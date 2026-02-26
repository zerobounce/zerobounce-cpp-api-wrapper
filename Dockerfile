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

RUN cmake -S . -B build -G "Unix Makefiles" \
    && cmake --build build

# Run tests via CTest
CMD ["ctest", "--test-dir", "build", "--output-on-failure"]
