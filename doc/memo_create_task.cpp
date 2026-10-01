
// Функция принимющая разный тип данных
static void anyTask(void* pvParameters)
{
    // 1. Приводим void* к указателю на int*
    // 2. Разыменовываем его с помощью *, чтобы получить само значение
    int my_number = *(int*)pvParameters; 

    // Альтернативный вариант в стиле C++ (более безопасный):
    // int my_number = *static_cast<int*>(pvParameters);

    // Теперь переменную можно использовать
    if (my_number == 5) {
        // ...
    }
} 