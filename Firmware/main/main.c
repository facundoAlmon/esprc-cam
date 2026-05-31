// Entry point — calls into C++ main task.
void app_task_start(void);

int app_main(void)
{
    app_task_start();
    return 0;
}
