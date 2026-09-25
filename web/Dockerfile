# syntax=docker/dockerfile:1

FROM node:24-bookworm-slim AS frontend
WORKDIR /source/frontend
COPY frontend/package*.json ./
RUN npm ci
COPY frontend/ ./
RUN npm run build

FROM rust:1-bookworm AS backend
WORKDIR /source/backend
COPY backend/Cargo.toml backend/Cargo.lock* ./
COPY backend/migrations ./migrations
COPY backend/src ./src
RUN --mount=type=cache,target=/usr/local/cargo/registry \
    --mount=type=cache,target=/usr/local/cargo/git \
    --mount=type=cache,target=/source/backend/target \
    cargo build --release --locked && \
    cp /source/backend/target/release/pulso-web-api /tmp/pulso-web-api

FROM debian:bookworm-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/*
RUN useradd --create-home --uid 10001 pulso
WORKDIR /app
COPY --from=backend /tmp/pulso-web-api /app/pulso-web-api
COPY --from=frontend /source/frontend/dist /app/frontend
USER pulso
ENV PULSO_WEB_BIND=0.0.0.0:8080 FRONTEND_DIR=/app/frontend SECURE_COOKIES=true
EXPOSE 8080
ENTRYPOINT ["/app/pulso-web-api"]
