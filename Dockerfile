FROM debian AS builder

RUN apt update && apt install build-essential libxml2-dev libseccomp-dev libcurl4-gnutls-dev -y
COPY . .
RUN make && make install

FROM debian
RUN apt update && apt install -y libcurl3-gnutls libxml2 lynx
COPY --from=builder /usr/local/bin/rdrview /usr/local/bin/rdrview
CMD ["/usr/local/bin/rdrview"]
