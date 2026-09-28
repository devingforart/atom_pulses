# syntax=docker/dockerfile:1

FROM node:24-bookworm-slim AS frontend
WORKDIR /source/frontend
COPY web/frontend/package*.json ./
RUN npm ci
COPY web/frontend/ ./
RUN npm run build

FROM rust:1-bookworm AS backend
WORKDIR /source/backend
COPY web/backend/Cargo.toml web/backend/Cargo.lock* ./
COPY web/backend/migrations ./migrations
COPY web/backend/src ./src
RUN --mount=type=cache,target=/usr/local/cargo/registry \
    --mount=type=cache,target=/usr/local/cargo/git \
    --mount=type=cache,target=/source/backend/target \
    cargo build --release --locked && \
    cp /source/backend/target/release/pulso-web-api /tmp/pulso-web-api

FROM debian:bookworm AS composer
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake g++ git make pkg-config libcurl4-openssl-dev \
    libfreetype-dev libfontconfig1-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
    libxinerama-dev libxrandr-dev libxrender-dev libxi-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /source
COPY CMakeLists.txt ./
COPY cmake ./cmake
COPY src ./src
COPY tools ./tools
RUN --mount=type=cache,target=/source/build \
    cmake -S . -B /source/build -DCMAKE_BUILD_TYPE=Release \
    -DPULSO_BUILD_PLUGIN=OFF -DPULSO_BUILD_TESTS=OFF -DPULSO_BUILD_CLI=OFF \
    -DPULSO_BUILD_CLOUD_WORKER=ON && \
    cmake --build /source/build --target pulso_cloud_worker --parallel 2 && \
    cp /source/build/pulso_cloud_worker /tmp/pulso_cloud_worker

FROM debian:bookworm-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates libcurl4 && rm -rf /var/lib/apt/lists/*
RUN useradd --create-home --uid 10001 pulso
WORKDIR /app
COPY --from=backend /tmp/pulso-web-api /app/pulso-web-api
COPY --from=composer /tmp/pulso_cloud_worker /app/pulso_cloud_worker
COPY --from=frontend /source/frontend/dist /app/frontend
RUN mkdir /app/cloud-jobs && chown pulso:pulso /app/cloud-jobs
USER pulso
ENV PULSO_WEB_BIND=0.0.0.0:8080 FRONTEND_DIR=/app/frontend SECURE_COOKIES=true \
    PULSO_CLOUD_WORKER_PATH=/app/pulso_cloud_worker PULSO_CLOUD_OUTPUT_DIR=/app/cloud-jobs \
    XDG_CONFIG_HOME=/app/cloud-jobs/xdg
EXPOSE 8080
ENTRYPOINT ["/app/pulso-web-api"]
