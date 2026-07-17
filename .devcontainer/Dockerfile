FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-22.04

RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y install --no-install-recommends \
        apt-transport-https ca-certificates dirmngr wget gnupg2 \
        libssl-dev cmake ninja-build gdb python3 python3-pip \
    && rm -rf /var/lib/apt/lists/*

RUN apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv 8919F6BD2B48D754 \
    && echo "deb https://packages.clickhouse.com/deb stable main" | tee /etc/apt/sources.list.d/clickhouse.list \
    && apt-get update \
    && apt-get install -y clickhouse-client \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install --no-cache-dir \
        clickhouse-connect \
        clickhouse-driver \
        pandas \
        numpy \
        scipy \
        orjson \
        pytest