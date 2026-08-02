# Multi-stage build: compile the C++ engine, then run a slim runtime.
FROM gcc:13 AS builder
WORKDIR /app
COPY engine/ engine/
COPY Makefile .
RUN make clean && make

FROM debian:bookworm-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends python3 python3-pip \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=builder /app/build/ome-cli /usr/local/bin/ome-cli
COPY server/ server/
WORKDIR /app/server
RUN pip install --no-cache-dir -r requirements.txt
EXPOSE 8000
CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]
