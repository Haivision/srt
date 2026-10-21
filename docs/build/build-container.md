# Building SRT Using Devcontainer or Docker

This guide describes how to use the provided Devcontainer or Dockerfile to set up a consistent build environment for SRT.

## VS Code Devcontainer (Recommended)

The devcontainer provides the easiest and most integrated development experience with VS Code.

### Prerequisites

- [Visual Studio Code](https://code.visualstudio.com/)
- [Docker Desktop](https://www.docker.com/products/docker-desktop) or Docker Engine
- [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) for VS Code

### Steps

1. Open the SRT repository in VS Code

   ```console
   $ code </path/to/srt>
   ```

2. Open in Container

   - VS Code should prompt you to "Reopen in Container"
   - Alternatively, press `F1` and select `Dev Containers: Reopen in Container`

3. Run Spell Check

   ```console
   $ cd </path/to/srt>
   $ codespell --config scripts/codespell/codespell.cfg
   ```

4. Build SRT Inside the Container

   ```console
   $ mkdir _build && cd _build
   $ cmake ../ -DCMAKE_COMPILE_WARNING_AS_ERROR=ON -DENABLE_STDCXX_SYNC=ON -DENABLE_ENCRYPTION=ON -DENABLE_UNITTESTS=ON -DENABLE_BONDING=ON -DENABLE_TESTING=ON -DENABLE_EXAMPLES=ON -DENABLE_CODE_COVERAGE=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   $ cmake --build . --parallel
   ```

5. Run Tests

   ```console
   $ cd _build
   $ ctest --extra-verbose
   ```

## Dockerfile

If you prefer not to use VS Code or want to use the Dockerfile independently, you can build and run the container manually.

### Prerequisites

- [Docker Desktop](https://www.docker.com/products/docker-desktop) or Docker Engine

### Steps

1. Build the Docker Image

   ```console
   $ cd </path/to/srt>
   $ docker build -t srt-build-env -f docker/Dockerfile .
   ```

2. Run the Container

   Mount the SRT source directory into the container:
   ```console
   $ docker run -it --rm -v "$(pwd)":/srt_build_env -w /srt_build_env srt-build-env
   ```

3. Run Spell Check

   ```console
   $ cd </path/to/srt>
   $ codespell --config scripts/codespell/codespell.cfg
   ```

4. Build SRT Inside the Container

   ```console
   $ mkdir _build && cd _build
   $ cmake ../ -DCMAKE_COMPILE_WARNING_AS_ERROR=ON -DENABLE_STDCXX_SYNC=ON -DENABLE_ENCRYPTION=ON -DENABLE_UNITTESTS=ON -DENABLE_BONDING=ON -DENABLE_TESTING=ON -DENABLE_EXAMPLES=ON -DENABLE_CODE_COVERAGE=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   $ cmake --build . --parallel
   ```

5. Run Tests

   ```console
   $ cd _build
   $ ctest --extra-verbose
   ```

6. Exit the Container

   ```console
   $ exit
   ```
