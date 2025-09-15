/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "can.h"
#include "iwdg.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "NRF24.h"
#include "NRF24_conf.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "NRF24_reg_addresses.h"
#include <math.h>
#include <stdbool.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ADC_MAX 4020    // 실제 최대값
#define ADC_MIN 0
#define ADC_NEU 2010	//ADC 중간값 4020/2
#define ADC_DEAD_ZONE 200	//데드존 처리 100

#define ROTATION_CONST -0.5f    // 회전 상수

#define RX_TIMEOUT_MS 100	//안정장치-100ms동안 조종기 신호가 없으면 통신이 끊겼다고 판단하고 모터를 정지시킴
// VESC Tool에서 설정한 최대 eRPM 값. 안전을 위해 코드에서도 제한을 둡니다.
#define MAX_ERPM 10000.0f


/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint8_t rx_address[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};

volatile uint8_t nrf_irq_flag = 0;
volatile uint8_t watchdog_flag = 0;

//system state
static uint32_t last_rx_ms = 0;
// --- pwm_active를 motor_active로 의미를 명확히 함 ---
static uint16_t motor_active = 0;


//이부분 봐서 지우던지 하기
#define PRINT_INTERVAL_MS 50      // 테라텀 줄당 출력 주기(ms)
volatile uint32_t exti_hits = 0;
uint32_t last_rawX = 0, last_rawY = 0, last_rawZ = 0;
static inline void kickdog(void) {
    HAL_IWDG_Refresh(&hiwdg);
}

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void CAN_SetERPM(uint8_t vesc_id, int32_t erpm);
void KiwiDrive_CAN(float vx, float vy, float omega);
void CAN_StopAll(void);
void nrf24_receiver_setup(void);
void system_watchdog_service(void);
void nrf24_irq_service(void); // <-- 이 줄 추가


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
extern IWDG_HandleTypeDef hiwdg;

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_SPI1_Init();
  MX_TIM1_Init();
  MX_TIM3_Init();
  MX_IWDG_Init();
  MX_CAN_Init();
  /* USER CODE BEGIN 2 */

  // --- CAN 설정 및 시작 코드 추가 ---
  CAN_FilterTypeDef can_filter;
  can_filter.FilterBank = 0;
  can_filter.FilterMode = CAN_FILTERMODE_IDMASK;
  can_filter.FilterScale = CAN_FILTERSCALE_32BIT;
  can_filter.FilterIdHigh = 0x0000;
  can_filter.FilterIdLow = 0x0000;
  can_filter.FilterMaskIdHigh = 0x0000;
  can_filter.FilterMaskIdLow = 0x0000;
  can_filter.FilterFIFOAssignment = CAN_RX_FIFO0;
  can_filter.FilterActivation = ENABLE;
  if (HAL_CAN_ConfigFilter(&hcan, &can_filter) != HAL_OK) {
      Error_Handler();
  }
  if (HAL_CAN_Start(&hcan) != HAL_OK) {
      Error_Handler();
  }
  if (HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
      Error_Handler();
  }
  // --- CAN 설정 끝 ---



  nrf24_init();




 nrf24_receiver_setup();



  HAL_TIM_Base_Start_IT(&htim3);
  HAL_Delay(10);


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

	    // 1. NRF 데이터 수신 처리 (가장 우선순위 높음)
	    if (nrf_irq_flag) {
	        nrf_irq_flag = 0;      // 플래그 즉시 내리기
	        nrf24_irq_service();   // 데이터 처리
	        kickdog();             // IWDG 리셋
	    }

	    // 2. TIM3 기반 주기적 작업 처리 (10ms 마다)
	    if (watchdog_flag) {
	    	;

	        watchdog_flag = 0;     // 플래그 즉시 내리기
	        system_watchdog_service(); // 통신 타임아웃 검사
	        // 여기에 나중에 추가할 주기적인 작업들을 넣으세요 (예: 배터리 체크)
	    }

	    // 3. 처리할 이벤트가 없으면 잠들기
	    // WFI는 어떤 인터럽트(SysTick, NRF, TIM3 등)가 발생하면 즉시 깨어납니다.
	    __WFI();

	}
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin){
	//nRF24L01 모듈이 데이터를 수신하면 PA4 IRQ핀에 하강엣지 트리거 발생, 위 콜백함수 호출

	if(GPIO_Pin == GPIO_PIN_8){	//이 인터럽트가 PA4핀에서 발생했으면

		nrf_irq_flag = 1;	//Main 루프에 데이터 도착 플래그 올림
	}
}

static inline float clampf(float x,float a,float b){
    return x<a?a:(x>b?b:x);
}

float NormalizeADC(int16_t raw){
    int16_t delta = raw - ADC_NEU;
    if(abs(delta) < ADC_DEAD_ZONE) return 0.0f;
    if(delta > 0) return (float)delta / (float)(ADC_MAX - ADC_NEU);
    else return (float)delta / (float)(ADC_NEU - ADC_MIN);
}


// --- KiwiDrive와 주변 함수를 CAN 기반으로 변경 ---


void CAN_SetERPM(uint8_t vesc_id, int32_t erpm) {
    CAN_TxHeaderTypeDef tx_header;
    uint8_t tx_data[8];
    uint32_t can_mailbox;

    tx_header.IDE = CAN_ID_EXT;
    tx_header.ExtId = (0x00000300 | vesc_id); // CAN_PACKET_SET_RPM command
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.DLC = 4;

    tx_data[0] = (erpm >> 24) & 0xFF;
    tx_data[1] = (erpm >> 16) & 0xFF;
    tx_data[2] = (erpm >> 8) & 0xFF;
    tx_data[3] = erpm & 0xFF;



    if (HAL_CAN_AddTxMessage(&hcan, &tx_header, tx_data, &can_mailbox) != HAL_OK) {
            printf("CAN TX FAILED!\r\n");
        } else {
            printf("CAN TX OK! ID:%d, ERPM:%ld\r\n", (int)vesc_id, erpm);
        }


   }


void KiwiDrive_CAN(float vx, float vy, float omega) {
    float Rw = -ROTATION_CONST * omega;

    float Mtop = vx + Rw;
    float Mbl = 0.866f * vy - 0.5f * vx + Rw;
    float Mbr = -0.866f * vy - 0.5f * vx + Rw;

    float maxM = fmaxf(fabsf(Mtop), fmaxf(fabsf(Mbl), fabsf(Mbr)));
    if (maxM > 1.0f) {
        Mtop /= maxM;
        Mbl  /= maxM;
        Mbr  /= maxM;
    }

    int32_t erpm_top = (int32_t)(Mtop * MAX_ERPM);
    //int32_t erpm_bl = (int32_t)(Mbl * MAX_ERPM);
    //int32_t erpm_br = (int32_t)(Mbr * MAX_ERPM);

    // VESC ID는 VESC Tool에서 설정한 값과 일치해야 합니다.
    CAN_SetERPM(1, erpm_top); // Top motor (ID 1)
    // --- VESC 1개 테스트 시에는 아래 2줄은 주석 처리 ---
    // CAN_SetERPM(2, erpm_bl);  // Bottom-Left motor (ID 2)
    // CAN_SetERPM(3, erpm_br);  // Bottom-Right motor (ID 3)
}


int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)&ch, 1, HAL_MAX_DELAY);
    return ch;
}


void nrf24_irq_service(void){

	nrf24_stop_listen();
	uint8_t st = nrf24_r_reg(STATUS, 1);


	if(st & (1<<6)){ // RX_DR (Data Ready) flag

		uint8_t buf[6];
		nrf24_receive(buf,6);

		uint16_t rawX = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
		uint16_t rawY = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
		uint16_t rawZ = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
		printf("RX ADC -> X:%u, Y:%u, Z:%u\r\n", rawX, rawY, rawZ);
		motor_active = 1;

		float vx = NormalizeADC((int16_t)rawX);
		float vy = NormalizeADC((int16_t)rawY);
		float omega = NormalizeADC((int16_t)rawZ);

		float erpm = sqrt(vx*vx + vy*vy) * MAX_ERPM; //ERPM 값 계산 예시
		printf("Calculated ERPM: %.2f\r\n", erpm);
		KiwiDrive_CAN(vx, vy, omega); // CAN 함수 호출로 변경

		last_rx_ms = HAL_GetTick();
		nrf24_clear_rx_dr();
	}
	nrf24_listen();
}
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef* htim){
	if(htim->Instance == TIM3){
		watchdog_flag = 1;
	}
}


void system_watchdog_service(void){
	if(motor_active){
		if((HAL_GetTick() - last_rx_ms) > RX_TIMEOUT_MS){
			CAN_StopAll(); // CAN 정지 함수 호출로 변경
		}
	}
}


void CAN_StopAll(void) {
    CAN_SetERPM(1, 0);
    // --- VESC 1개 테스트 시에는 아래 2줄은 주석 처리 ---
    // CAN_SetERPM(2, 0);
    // CAN_SetERPM(3, 0);
    motor_active = 0;
}
void nrf24_receiver_setup(void)
{

    nrf24_defaults();                               //레지스터 기본값으로 리셋
    HAL_Delay(5);                                   //전원, spi안정화 대기(최소 4.5ms이상 필요)
    nrf24_stop_listen();

    nrf24_pwr_up();
    HAL_Delay(5);


    nrf24_set_channel(40);
                       //무선 채널 40설정
    nrf24_auto_ack_all(disable);
    nrf24_dpl(disable);
    nrf24_data_rate(_1mbps);
    nrf24_set_payload_size(6);
    nrf24_rx_mode();


    uint8_t cfg = nrf24_r_reg(CONFIG, 1);
    cfg &= ~((1<<6) | (1<<5) | (1<<4));
    nrf24_w_reg(CONFIG, &cfg, 1);

    nrf24_open_rx_pipe(0, rx_address);
    nrf24_listen();
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_8);
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h){
    CAN_RxHeaderTypeDef rx;
    uint8_t d[8];
    if (HAL_CAN_GetRxMessage(h, CAN_RX_FIFO0, &rx, d)==HAL_OK){
        if (rx.IDE == CAN_ID_EXT){
        	printf("RX EID=0x%08lX DLC=%ld\r\n", (unsigned long)rx.ExtId, (unsigned long)rx.DLC);
        }
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
	__disable_irq();

  /* User can add his own implementation to report the HAL error return state */

  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
