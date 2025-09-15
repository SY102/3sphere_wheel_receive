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
static uint16_t last_rx_ms = 0;
static uint16_t pwm_active = 0;


//이부분 봐서 지우던지 하기
#define PRINT_INTERVAL_MS 50      // 테라텀 줄당 출력 주기(ms)
volatile uint32_t exti_hits = 0;
uint32_t last_rawX = 0, last_rawY = 0, last_rawZ = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

void nrf24_receiver_setup(void);
void nrf24_irq_service(void);
void system_watchdog_service(void);
void PWM_Start(void);
void PWM_StopAll(void);
void KiwiDrive(float vx, float vy, float omega);
void DebugUART(uint16_t rawX, uint16_t rawY, uint16_t rawZ);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  /* USER CODE BEGIN 2 */
  nrf24_init();
  nrf24_receiver_setup();
  HAL_TIM_Base_Start_IT(&htim3);	//TIM3 시작
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

	  //---데이터 수신 이벤트 처리---
	 	  if(nrf_irq_flag){
	 		  nrf_irq_flag = 0;	//flag 내리기
	 		  nrf24_irq_service();	//데이터 처리 함수 호출, 모터 제어
	 	  }
	 	  //---TIM3기반 와치독 이벤트 처리---
	 	  if(watchdog_flag){
	 		  watchdog_flag = 0;	//확인 후 flag 내림
	 		  system_watchdog_service();	//와치독 함수 호출
	 	  }

	 	  //---저전력 모드 진입(WFI)---
	 	  if(!nrf_irq_flag && !watchdog_flag){	//두개의 flag가 내려가 있으면 처리할 이벤트가 없으므로 WFI
	 		  __WFI();
	 	  }
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

	if(GPIO_Pin == GPIO_PIN_4){	//이 인터럽트가 PA4핀에서 발생했으면
		nrf_irq_flag = 1;	//Main 루프에 데이터 도착 플래그 올림
	}
}

static inline float clampf(float x,float a,float b){
    return x<a?a:(x>b?b:x);
}

float NormalizeADC(int16_t raw){
    int16_t delta = raw - ADC_NEU;

    if(abs(delta) < ADC_DEAD_ZONE){
        return 0.0f;
    }

    if(delta > 0) {
        // CW 방향: 중간값~최대값 → 0~1
        return (float)delta / (float)(ADC_MAX - ADC_NEU);  // /2010
    } else {
        // CCW 방향: 최소값~중간값 → -1~0
        return (float)delta / (float)(ADC_NEU - ADC_MIN);  // /2010
    }
}

uint16_t ToPWMus(float v){
	if(v > 1.0f){
		v = 1.0f;
	}else if(v < -1.0f){
		v = -1.0f;
	}
	return(uint16_t)((v + 1.0f) * 500.0f + 1000.0f);
}


void KiwiDrive(float vx, float vy, float omega){
    float Rw = -ROTATION_CONST * omega;

    float Mtop = vx + Rw;
    float Mbl = 0.866f*vy - 0.5f*vx + Rw;
    float Mbr = -0.866f*vy - 0.5f*vx + Rw;

    float maxM = fmaxf(fabsf(Mtop), fmaxf(fabsf(Mbl), fabsf(Mbr)));
    if (maxM > 1.0f) {
        Mtop /= maxM;
        Mbl  /= maxM;
        Mbr  /= maxM;
    }

    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, ToPWMus(Mtop)); // PA9
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, ToPWMus(Mbl));  // PA10
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, ToPWMus(Mbr));  // PA11
}

void DebugUART(uint16_t rawX, uint16_t rawY, uint16_t rawZ){
    char buf[128];
    uint16_t pwm1 = __HAL_TIM_GET_COMPARE(&htim1, TIM_CHANNEL_2);
    uint16_t pwm2 = __HAL_TIM_GET_COMPARE(&htim1, TIM_CHANNEL_3);
    uint16_t pwm3 = __HAL_TIM_GET_COMPARE(&htim1, TIM_CHANNEL_4);

    int len = snprintf(buf, sizeof(buf),
            "rawX:%4lu PWM_top:%4lu | rawY:%4lu PWM_bl:%4lu | rawZ:%4lu PWM_br:%4lu\r\n",
            (unsigned long)rawX, (unsigned long)pwm1,
            (unsigned long)rawY, (unsigned long)pwm2,
            (unsigned long)rawZ, (unsigned long)pwm3);
    HAL_UART_Transmit(&huart2, (uint8_t*)buf, len, HAL_MAX_DELAY);
}

void debug_dump_settings(void)                   //주소 확인용
{
    uint8_t ch  = nrf24_r_reg(RF_CH,      1);    // RF 채널 번호
    uint8_t pw  = nrf24_r_reg(RX_PW_P0,   1);    // 파이프0 수신 페이로드 크기
    uint8_t addr[5];


    csn_low();
    {
        uint8_t cmd = R_REGISTER | RX_ADDR_P0;
        HAL_SPI_Transmit(&hspi1, &cmd,     1, 100);
        HAL_SPI_Receive (&hspi1, addr,     5, 100);
    }
    csn_high();


    printf("=== NRF24 DEBUG ===\r\n");
    printf(" RF_CH       = %u\r\n",      ch);
    printf(" RX_PW_P0    = %u bytes\r\n", pw);
    printf(" RX_ADDR_P0  = %02X %02X %02X %02X %02X\r\n",
           addr[0], addr[1], addr[2], addr[3], addr[4]);
    printf("==================\r\n");
}

int32_t Set_PWM_Duty(uint32_t adc_value)
{
    int32_t us;                                                    // VESC로 보낼 마이크로초 펄스 값


    if (abs((int)adc_value - ADC_NEU) < ADC_DEAD_ZONE) {           //데드존 처리
        us = 1500;
    } else {
        us = (int)(((float)adc_value / 4095.0f) * 1000.0f + 1000.0f);


        if (us < 1000) us = 1000;                                  //매핑 후 범위 클램핑 (1000~2000 사이로 유지)
        if (us > 2000) us = 2000;
    }

                                                                   // TIM1의 채널1 (PA8)의 듀티비 업데이트

    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (uint32_t)us);

    return us;
}



int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)&ch, 1, HAL_MAX_DELAY);
    return ch;
}


bool try_receive_nrf24(uint32_t *rawX, uint32_t *rawY, uint32_t *rawZ)
{
    if (!nrf24_data_available()) return false;

    uint8_t buf[6];
    nrf24_receive(buf, 6);

    *rawX = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    *rawY = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
    *rawZ = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);

    nrf24_clear_rx_dr();
    return true;
}


void nrf24_irq_service(void){
	nrf24_stop_listen();
	uint8_t st = nrf24_r_reg(STATUS, 1);

	if(st & (1<<6)){
		uint8_t buf[6];
		nrf24_receive(buf,6);

		// 6바이트 2진 언패킹(unpacking)
		uint16_t rawX = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
		uint16_t rawY = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
		uint16_t rawZ = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
		DebugUART(rawX, rawY, rawZ);

		if(!pwm_active){
			PWM_Start();
		}

		//읽어온 값을 실제 모터 제어값으로 정규화
		float vx = NormalizeADC((int16_t)rawX); // 부호 있어야함
		float vy = NormalizeADC((int16_t)rawY);
		float omega = NormalizeADC((int16_t)rawZ);

		//변환된 값으로 키위 드라이브 알고리즘을 실행, 모터 구동
		KiwiDrive(vx, vy, omega);

		//TIM3 워치독을 위해 마지막으로 데이터를 수신한 시간을 현재시간으로 갱신
		last_rx_ms = HAL_GetTick();

		//nrf24의 RX_DR상태 비터를 0으로 claer, 다음 인터럽트 받을 준비
		nrf24_clear_rx_dr();
	}

	nrf24_listen();	//데이터 수신 대기모드

}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef* htim){
	//50ms마다 발생하는 인터럽트(TIM3)
	if(htim->Instance == TIM3){	//이 인터럽트가 TIM3에서 발생했으면
		watchdog_flag = 1;	//Main 루프에 점검할 시간이라고 플래그 올림
	}
}

void system_watchdog_service(void){	//워치독 서비스
	if(pwm_active){
		//모터가 동작 중일때만 감시 수행
		if((HAL_GetTick() - last_rx_ms) > RX_TIMEOUT_MS){	//현재시간과 마지막 데이터 수신 시간의 차이가 타임아웃(100ms)를 초과했다면
			PWM_StopAll();	//모든 모터 정지
		}
	}
}

void PWM_Start(void){
	HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
	HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
	HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);

	pwm_active = 1;	//시스템 상태를 pwm 활성화로 변경하는 플래그
}

//모든 채널의 PWM 완전 정지
void PWM_StopAll(void){
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_4);

    pwm_active = 0;
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

    nrf24_set_payload_size(6);
    nrf24_rx_mode();


    uint8_t cfg = nrf24_r_reg(CONFIG, 1);
    cfg &= ~((1<<6) | (1<<5) | (1<<4));
    nrf24_w_reg(CONFIG, &cfg, 1);

    nrf24_open_rx_pipe(0, rx_address);
    nrf24_listen();
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_4);
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
