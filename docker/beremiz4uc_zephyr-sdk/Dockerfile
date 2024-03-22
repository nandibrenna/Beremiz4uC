#****************************************************************************
#  Project Name: Beremiz 4 uC                                               #
#  Author(s): nandibrenna                                                   #
#  Created: 2024-03-15                                                      #
#  ======================================================================== #
#  Copyright © 2024 nandibrenna                                             #
#                                                                           #
#  Licensed under the Apache License, Version 2.0 (the "License");          #
#  you may not use this file except in compliance with the License.         #
#  You may obtain a copy of the License at                                  #
#                                                                           #
#      http://www.apache.org/licenses/LICENSE-2.0                           #
#                                                                           #
#  Unless required by applicable law or agreed to in writing, software      #
#  distributed under the License is distributed on an "AS IS" BASIS,        #
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or          #
#  implied. See the License for the specific language governing             #
#  permissions and limitations under the License.                           #
#                                                                           #
#***************************************************************************/
FROM ubuntu:22.04
LABEL org.opencontainers.image.title="Beremiz4uC" \
      org.opencontainers.image.description="Development environment for Zephyr RTOS projects with Beremiz4uC." \
      org.opencontainers.image.authors="nandibrenna" \
      org.opencontainers.image.licenses="Apache-2.0"
	  
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    git cmake ninja-build gperf ccache dfu-util \
    device-tree-compiler wget python3-dev python3-pip \
    python3-setuptools python3-tk python3-wheel xz-utils \
    file make gcc gcc-multilib g++-multilib libsdl2-dev \
    libmagic1 build-essential sudo \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install west

RUN useradd -m docker && echo "docker:docker" | chpasswd && adduser docker sudo \
    && echo 'docker ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

USER docker
WORKDIR /home/docker

RUN wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.5/zephyr-sdk-0.16.5_linux-x86_64_minimal.tar.xz \
    && tar xvf zephyr-sdk-0.16.5_linux-x86_64_minimal.tar.xz \
    && ./zephyr-sdk-0.16.5/setup.sh -t arm-zephyr-eabi -h -c \
    && rm zephyr-sdk-0.16.5_linux-x86_64_minimal.tar.xz

RUN wget https://github.com/EmbeddedRPC/erpc/releases/download/1.12.0/Linux_x64.zip \
    && unzip Linux_x64.zip -d /usr/local/bin/ \
    && chmod +x /usr/local/bin/erpcgen \
    && rm Linux_x64.zip

ARG REPO_URL
ARG ACCESS_TOKEN
RUN if [ -z "$ACCESS_TOKEN" ]; then \
    west init -m https://$REPO_URL workspace; \
    else \
    west init -m https://oauth2:$ACCESS_TOKEN@$REPO_URL workspace; \
    fi \
    && cd workspace \
    && west update \
    && pip3 install -r zephyr/scripts/requirements.txt \
    && west zephyr-export

WORKDIR /home/docker/workspace

COPY start.sh /home/docker/start.sh
CMD ["/home/docker/start.sh"]
