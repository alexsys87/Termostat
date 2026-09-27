#pragma language=extended

static void ResetISR(void);
static void NmiISR(void);
static void FaultISR(void);
static void SVCallISR(void);
static void PendSVISR(void);
static void IntDefaultHandler(void);

void SysTick_Handler(void);
void TIM14_IRQHandler(void);

extern void __iar_program_start(void);

extern void CSTACK$$Limit(void);

typedef union
{
    void (*pfnHandler)(void);
    unsigned long ulPtr;
} uVectorEntry;

__root const uVectorEntry __vector_table[] @ ".intvec" =
{
    { .ulPtr = (unsigned long)&CSTACK$$Limit },  // Top of the stack
    ResetISR,                                                                   // -15
    NmiISR,                                                                     // -14
    FaultISR,                                                                   // -13
    0,                                                                          // -12
    0,                                                                          // -11
    0,                                                                          // -10
    0,                                                                          // -9
    0,                                                                          // -8
    0,                                                                          // -7
    0,                                                                          // -6
    SVCallISR,                                                                  // -5
    0,                                                                          // -4
    0,                                                                          // -3
    PendSVISR,                                                                  // -2
    SysTick_Handler,                                                            // -1 

    IntDefaultHandler,                                                          // 0
    IntDefaultHandler,                                                          // 1
    IntDefaultHandler,                                                          // 2
    IntDefaultHandler,                                                          // 3
    IntDefaultHandler,                                                          // 4
    IntDefaultHandler,                                                          // 5
    IntDefaultHandler,                                                          // 6
    IntDefaultHandler,                                                          // 7
    IntDefaultHandler,                                                          // 8
    IntDefaultHandler,                                                          // 9
    IntDefaultHandler,                                                          // 10
    IntDefaultHandler,                                                          // 11
    IntDefaultHandler,                                                          // 12
    IntDefaultHandler,                                                          // 13
    IntDefaultHandler,                                                          // 14
    IntDefaultHandler,                                                          // 15
    IntDefaultHandler,                                                          // 16
    IntDefaultHandler,                                                          // 17
    0,                                                                          // 18
    TIM14_IRQHandler,                                                          // 19
    IntDefaultHandler,                                                          // 20
    IntDefaultHandler,                                                          // 21
    IntDefaultHandler,                                                          // 22
    IntDefaultHandler,                                                          // 23
    IntDefaultHandler,                                                          // 24
    IntDefaultHandler,                                                          // 25
    IntDefaultHandler,                                                          // 26
    IntDefaultHandler,                                                          // 27
    IntDefaultHandler,                                                          // 28
    0,                                                                          // 29
    IntDefaultHandler,                                                          // 30
    0,                                                                          // 31
};

static void ResetISR(void)
{
    __iar_program_start();
}

static void NmiISR(void)          { while(1); }
static void FaultISR(void)        { while(1); }
static void SVCallISR(void)       { while(1); }
static void PendSVISR(void)       { while(1); }
static void IntDefaultHandler(void) { while(1); }

__weak void SysTick_Handler(void) { }  
