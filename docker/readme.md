
# Beremiz4uC Zephyr SDK Docker Container

This document guides you through the process of building and launching the `beremiz4uc_zephyr-sdk` Docker container designed for development with the Zephyr SDK.

## Prerequisites

Ensure Docker is installed on your system. Visit the official [Docker website](https://docs.docker.com/get-docker/) for installation instructions.

## Building the Container

1. Open a terminal and navigate to the directory containing the `Dockerfile` and `start.sh`.

2. Execute the following command to build the Docker image. Replace `<path-to-directory>` with the actual path to the directory if you are not already there:

    ```sh
    docker build -t beremiz4uc_zephyr-sdk .
    ```

    This command builds the Docker image named `beremiz4uc_zephyr-sdk`, based on the instructions in the `Dockerfile`.

## Starting the Container

After the image has been successfully built, you can start the container as follows:

```sh
docker run -it beremiz4uc_zephyr-sdk
```

Executing this command starts the container in interactive mode, granting you access to a Bash shell inside the container. Upon the first launch, you will be greeted with informational messages defined in the `start.sh` script.

## Using REPO_URL and ACCESS_TOKEN

When creating the container, you might want to clone a specific repository and perhaps use an access token for private repositories. You can specify these using the `--build-arg` option during the build process. Here's how to include the `REPO_URL` and an example of using a public Git repository:

```sh
docker build --build-arg REPO_URL=https://github.com/nandibrenna/Beremiz4uC -t beremiz4uc_zephyr-sdk .
```

For private repositories requiring an access token, you would also include `ACCESS_TOKEN`:

```sh
docker build --build-arg REPO_URL=https://github.com/nandibrenna/Beremiz4uC --build-arg ACCESS_TOKEN=your_access_token_here -t beremiz4uc_zephyr-sdk .
```

Remember to replace `your_access_token_here` with your actual GitHub access token.

## Additional Information

- To exit the container after use, type `exit` in the container's terminal session.
- To check which Docker containers are currently running, use `docker ps`.
- For more information on using Docker and the available commands, visit the [Docker documentation](https://docs.docker.com/).
