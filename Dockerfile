# ZeroBounce C++ API – test image (CMake + Google Test)
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure

RUN useradd --uid 65532 --create-home --shell /usr/sbin/nologin zbtest \
    && chown -R zbtest:zbtest /app

USER zbtest

CMD ["ctest", "--test-dir", "build", "--output-on-failure"]
