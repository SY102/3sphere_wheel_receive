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

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
const int ADC_NEU = 2047;
const int ADC_DEAD_ZONE = 100;
#define ROTATION_CONST 0.5f
#define USE_ADC_FALLBACK 1  // 1이면 무선 없을 때 ADC로 대체

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
uint8_t rx_address[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};

float NormalizeADC();
uint32_t ToPWMus(float v);
void KiwiDrive(float vx, float vy, float omega);
void DebugUART();
void debug_dump_settings();
int32_t Set_PWM_Duty();
void nrf24_receiver_setup();
bool try_receive_nrf24();

extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim1;
extern UART_HandleTypeDef huart2;


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

  /* USER CODE BEGIN 2 */
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);

     // nRF24 초기화 (수신기)
  nrf24_init();

  nrf24_receiver_setup();



  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */


  // USER CODE BEGIN 2



  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	//  debug_dump_settings();


	  uint32_t rawX = 0, rawY = 0, rawZ = 0;
	          if (try_receive_nrf24(&rawX, &rawY, &rawZ)) {
	              float vx = NormalizeADC((int32_t)rawX - ADC_NEU);
	              float vy = NormalizeADC((int32_t)rawY - ADC_NEU);
	              float omega = NormalizeADC((int32_t)rawZ - ADC_NEU);

	              KiwiDrive(vx, vy, omega);
	              DebugUART(rawX, rawY, rawZ);
	          }
	          // 수신 없을 땐 이전 상태 유지하거나 필요하면 멈추게 처리 가능
	          HAL_Delay(10);



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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
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

float NormalizeADC(int32_t delta){
    if(abs(delta) < ADC_DEAD_ZONE){
        return 0.0f;
    } else {
        return (float)delta / (float)ADC_NEU;
    }
}

uint32_t ToPWMus(float v){
    if(v > 1.0f) v = 1.0f;
    else if(v < -1.0f) v = -1.0f;
    return (uint32_t)((v + 1.0f) * 500.0f + 1000.0f);
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

void DebugUART(uint32_t rawX, uint32_t rawY, uint32_t rawZ){
    char buf[128];
    uint32_t pwm1 = __HAL_TIM_GET_COMPARE(&htim1, TIM_CHANNEL_2);
    uint32_t pwm2 = __HAL_TIM_GET_COMPARE(&htim1, TIM_CHANNEL_3);
    uint32_t pwm3 = __HAL_TIM_GET_COMPARE(&htim1, TIM_CHANNEL_4);

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
    //ack 비활성화
    nrf24_set_payload_size(6);
    nrf24_rx_mode();

   //nrf24_dpl(enable);                              // FEATURE 레지스터: DPL 켜기
    //nrf24_set_rx_dpl(0, enable);                    // 파이프0에 대해서만 DPL 사용


    nrf24_open_rx_pipe(0, rx_address);              //파이프 0에 수신주소 설정


    nrf24_listen();                                 //CE=high => 실제 수신 대기모드 진입



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
