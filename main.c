#include <stdio.h>
#include "thread_safe_bus.h"
#include "test_thread_safe_bus.h"


int main() {
    printf("Hello, World!\n");
    
    int result = test_thread_safe_bus();
    printf("Test result: %d\n", result);

    return 0;
}