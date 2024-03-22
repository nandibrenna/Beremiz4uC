#!/bin/bash

# Output information messages
echo "Welcome to the Beremiz4uC Zephyr SDK Docker Container!"
echo "To build your project, execute the following command:"
echo "west build -p always -b PLC_STM32F407VE beremiz4uc/app"

# Execute an interactive Bash shell to use the container interactively
exec /bin/bash
