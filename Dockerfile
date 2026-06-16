# ─────────────────────────────────────────────────────────────────
# Dockerfile pour Backend_Seastar (SeaDesktop)
# ─────────────────────────────────────────────────────────────────
#
# Strategie multi-stage :
#   - Stage "seastar"  : compile Seastar depuis les sources GitHub.
#                        Cette couche est tres lourde (~20 min) mais
#                        ne sera reconstruite que si SEASTAR_COMMIT
#                        change. Cache Docker tres efficace ici.
#   - Stage "backend"  : utilise l'image seastar, copie le code SeaDesktop,
#                        compile backend_seastar. Couche legere a
#                        recompiler (~2 min) sur changement de code.
#   - Stage "runtime"  : image finale minimale (~150 Mo), copie juste
#                        le binaire et les libs runtime.
#
# Pour rebuilder Seastar (changement de version) :
#   docker compose build --no-cache service_a
#
# Pour un rebuild rapide apres modification du code SeaDesktop :
#   docker compose build service_a
# (Docker reutilise le stage seastar deja compile.)
#
# Usage standard via docker-compose.yml.
# ─────────────────────────────────────────────────────────────────


# Version de Seastar a epingler. Tag de base seastar-25.05.0 + commits
# additionnels. Pour mettre a jour, changer ce SHA.
ARG SEASTAR_COMMIT=a2dd373e


# ═════════════════════════════════════════════════════════════════
# Stage 1 : seastar
# ─────────────────────────────────────────────────────────────────
# Clone et compile Seastar. Cette couche est volumineuse mais cachee
# tant que SEASTAR_COMMIT ne change pas.
# ═════════════════════════════════════════════════════════════════
FROM ubuntu:24.04 AS seastar

ENV DEBIAN_FRONTEND=noninteractive

# Dependances de build communes (seastar + backend ensuite).
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        ninja-build \
        git \
        pkg-config \
        ragel \
        libboost-all-dev \
        libc-ares-dev \
        libcrypto++-dev \
        libfmt-dev \
        libhwloc-dev \
        liblz4-dev \
        libnuma-dev \
        libprotobuf-dev \
        libssl-dev \
        libsystemd-dev \
        libxml2-dev \
        libyaml-cpp-dev \
        protobuf-compiler \
        python3 \
        python3-yaml \
        systemtap-sdt-dev \
        valgrind \
        xfslibs-dev \
    && rm -rf /var/lib/apt/lists/*

# Clone du commit Seastar epingle.
ARG SEASTAR_COMMIT
RUN git clone https://github.com/scylladb/seastar.git /opt/seastar \
    && cd /opt/seastar \
    && git checkout ${SEASTAR_COMMIT}

# Installation des deps Seastar non couvertes ci-dessus.
# Le script install-dependencies.sh detecte la distro et installe
# ce qui manque (specifique a chaque version Seastar). On refait
# apt-get update car le RUN precedent a peut-etre supprime le cache,
# et on garde ce cache pour le stage backend en aval.
RUN apt-get update \
    && cd /opt/seastar \
    && ./install-dependencies.sh

# Compilation Seastar en mode release.
# - --without-tests : on ne compile pas les tests Seastar (gain
#   massif : ~300 fichiers en moins sur 401, et evite des g++ qui
#   peuvent consommer 1-2 Go chacun).
# - -j 2 : limite a 2 jobs paralleles pour ne pas OOM sur les
#   machines moyennement dotees. Ajuster selon la RAM disponible
#   (1 job par ~3 Go de RAM est une bonne regle).
RUN cd /opt/seastar \
    && ./configure.py --mode=release --without-tests --without-demos --without-apps \
    && ninja -C build/release -j 2 \
    && cmake --install build/release


# ═════════════════════════════════════════════════════════════════
# Stage 2 : backend (compilation de backend_seastar)
# ─────────────────────────────────────────────────────────────────
# Reprend l'image seastar et compile le code SeaDesktop. Cette etape
# est rapide (~2 min) sur changement de code, car Seastar est deja
# installe dans /usr/local.
# ═════════════════════════════════════════════════════════════════
FROM seastar AS backend

# Dependances specifiques au backend SeaDesktop (au-dela de Seastar).
RUN apt-get update && apt-get install -y --no-install-recommends \
        libmysqlclient-dev \
        libspdlog-dev \
        nlohmann-json3-dev \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# Copie du source. Le .dockerignore exclut build/, .git/, etc.
WORKDIR /src
COPY . .

# -DBUILD_SEAUI=OFF : on ne compile pas SeaUI dans l'image backend.
#   SeaUI necessite Qt6 (~500 Mo de deps), inutile au runtime serveur.
# -j 2 : limite le parallelisme pour eviter l'OOM (cf. stage seastar).
RUN mkdir -p build \
    && cmake -S . -B build \
             -G Ninja \
             -DCMAKE_BUILD_TYPE=Release \
             -DBUILD_SEAUI=OFF \
    && cmake --build build --target backend_seastar -j 2


# ═════════════════════════════════════════════════════════════════
# Stage 3 : runtime
# ─────────────────────────────────────────────────────────────────
# Image finale. Contient uniquement le binaire et les libs runtime.
# Pas de compilateurs, pas de headers, pas de sources.
# ═════════════════════════════════════════════════════════════════
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Libs runtime uniquement.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libmysqlclient21 \
        libfmt9 \
        libspdlog1.12 \
        libssl3t64 \
        libboost-program-options1.83.0 \
        libboost-thread1.83.0 \
        libboost-system1.83.0 \
        libboost-filesystem1.83.0 \
        libboost-iostreams1.83.0 \
        libc-ares2 \
        libhwloc15 \
        liblz4-1 \
        libnuma1 \
        libprotobuf32t64 \
        libxml2 \
        libyaml-cpp0.8 \
        liburing2 \
        ca-certificates \
        tzdata \
    && rm -rf /var/lib/apt/lists/*

# Utilisateur non-root pour le runtime.
# Ubuntu 24.04 cree un user "ubuntu" UID/GID 1000 par defaut ; on le
# supprime avant de creer seadesktop pour reutiliser ces IDs (qui
# correspondent souvent au user host en mode dev pour la coherence
# des permissions sur les volumes montes).
RUN (userdel -r ubuntu 2>/dev/null || true) \
    && groupadd --gid 1000 seadesktop \
    && useradd --uid 1000 --gid seadesktop --shell /bin/bash --create-home seadesktop

# Repertoires applicatifs.
RUN mkdir -p /app /app/configs /app/logs \
    && chown -R seadesktop:seadesktop /app

# Copie du binaire et des libs Seastar depuis le stage backend.
COPY --from=backend --chown=seadesktop:seadesktop \
     /src/build/apps/Backend_Seastar/backend_seastar /app/backend_seastar
     
# Copie de toutes les libs mariadb-connector-cpp custom utilisees par
# le backend (libmariadbcpp.so + ses dependances transitives :
# libmariadb.so.3, plugins, etc.).
COPY --from=backend --chown=root:root \
     /src/third_party/mariadb-connector-cpp/install/lib/mariadb/ \
     /usr/local/lib/mariadb/

# Enregistrement du dossier dans ldconfig pour que le loader trouve
# les .so au runtime.
RUN echo "/usr/local/lib/mariadb" > /etc/ld.so.conf.d/mariadb.conf \
    && ldconfig

# Seastar est statiquement linke dans backend_seastar (libseastar.a),
# donc pas besoin de copier libseastar* dans le runtime. Verifie au
# runtime avec : ldd /app/backend_seastar | grep seastar (devrait etre vide).

WORKDIR /app

VOLUME ["/app/configs", "/app/logs"]

EXPOSE 8080

USER seadesktop

# Pas d'ENTRYPOINT/CMD par defaut : c'est le docker-compose qui
# specifie --config et --service_name. La meme image sert tous les
# services.
ENTRYPOINT ["/app/backend_seastar"]
