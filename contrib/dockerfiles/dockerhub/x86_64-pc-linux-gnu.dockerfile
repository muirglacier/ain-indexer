FROM defichain-x86_64-pc-linux-gnu:dockerhub-latest as dh-build

FROM debian:10-slim
ENV PATH=/app/bin:$PATH
WORKDIR /app

COPY --from=dh-build /app/. ./

RUN useradd --create-home defi && \
    mkdir -p /data && \
    chown defi:defi /data && \
    ln -s /blockchain /home/defi/.defi
COPY ./bin/dfi_init ./bin/dfi_oneshot /app/bin/

RUN chmod 755 /app/bin/dfi_init
RUN chmod 755 /app/bin/dfi_oneshot

VOLUME ["/blockchain"]

USER defi:defi
CMD [ "/app/bin/dfi_oneshot" ]

EXPOSE 8555 8554 18555 18554 19555 19554
