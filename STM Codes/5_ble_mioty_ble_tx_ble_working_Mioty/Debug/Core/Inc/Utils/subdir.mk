################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Core/Inc/Utils/utils.cpp 

OBJS += \
./Core/Inc/Utils/utils.o 

CPP_DEPS += \
./Core/Inc/Utils/utils.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Inc/Utils/%.o Core/Inc/Utils/%.su Core/Inc/Utils/%.cyclo: ../Core/Inc/Utils/%.cpp Core/Inc/Utils/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m3 -std=gnu++14 -g3 -c -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-Inc-2f-Utils

clean-Core-2f-Inc-2f-Utils:
	-$(RM) ./Core/Inc/Utils/utils.cyclo ./Core/Inc/Utils/utils.d ./Core/Inc/Utils/utils.o ./Core/Inc/Utils/utils.su

.PHONY: clean-Core-2f-Inc-2f-Utils

