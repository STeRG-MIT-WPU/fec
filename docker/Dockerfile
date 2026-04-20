# fec_sterg verification image
#
# Contains:
#   * gcc        - for host-side functional tests
#   * arm-none-eabi-gcc - for STM32 cross-compile sanity check
#   * make       - to drive the build
#
# Default CMD runs both `host-test` and `arm-build` and exits non-zero if
# either fails.

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        gcc-arm-none-eabi \
        binutils-arm-none-eabi \
        libnewlib-arm-none-eabi \
        make \
        ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /work

# The source tree is mounted at /work/fec_sterg at run-time.
# Run `make -C fec_sterg/test all` to verify.
CMD ["make", "-C", "fec_sterg/test", "all"]
