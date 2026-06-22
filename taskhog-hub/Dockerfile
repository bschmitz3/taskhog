FROM python:3.11-slim-bookworm

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    curl \
    ffmpeg \
    && rm -rf /var/lib/apt/lists/*

COPY pyproject.toml ./
COPY app ./app
COPY scripts ./scripts

RUN pip install --no-cache-dir .

EXPOSE 8088

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8088"]
