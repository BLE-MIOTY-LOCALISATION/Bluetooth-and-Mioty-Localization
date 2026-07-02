################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/SX1280_HAL_Driver/Src/sx1280_hal.c 

OBJS += \
./Drivers/SX1280_HAL_Driver/Src/sx1280_hal.o 

C_DEPS += \
./Drivers/SX1280_HAL_Driver/Src/sx1280_hal.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/SX1280_HAL_Driver/Src/%.o Drivers/SX1280_HAL_Driver/Src/%.su Drivers/SX1280_HAL_Driver/Src/%.cyclo: ../Drivers/SX1280_HAL_Driver/Src/%.c Drivers/SX1280_HAL_Driver/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB -c -I../Core/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Drivers-2f-SX1280_HAL_Driver-2f-Src

clean-Drivers-2f-SX1280_HAL_Driver-2f-Src:
	-$(RM) ./Drivers/SX1280_HAL_Driver/Src/sx1280_hal.cyclo ./Drivers/SX1280_HAL_Driver/Src/sx1280_hal.d ./Drivers/SX1280_HAL_Driver/Src/sx1280_hal.o ./Drivers/SX1280_HAL_Driver/Src/sx1280_hal.su

.PHONY: clean-Drivers-2f-SX1280_HAL_Driver-2f-Src

