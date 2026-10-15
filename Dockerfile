# Etapa 1: compilar
FROM debian:bookworm-slim AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ libsdl2-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY analizador_audio.cpp .
RUN g++ -O2 -std=c++17 analizador_audio.cpp -o analizador $(sdl2-config --cflags --libs)

# Etapa 2: imagen final solo con lo necesario para ejecutar
FROM debian:bookworm-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends libsdl2-2.0-0 libpulse0 libasound2 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/analizador /usr/local/bin/analizador
# capturas/ y record.txt se escriben en el directorio de trabajo
WORKDIR /data
VOLUME /data
ENTRYPOINT ["analizador"]
