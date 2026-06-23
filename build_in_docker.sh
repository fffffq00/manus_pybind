#!/bin/bash
# Stop script on first error
set -e

# Get Python version from command line arguments, default to 3.13
PYTHON_VERSION=$1
if [ -z "$PYTHON_VERSION" ]; then
    PYTHON_VERSION="3.13"
fi

IMAGE_TAG="manus-builder-base"



# Detect local proxy environment variables on the host and pass them to Docker (only if defined)
PROXY_ARGS=""
PROXY_RUN_ARGS=""
for var in http_proxy https_proxy ftp_proxy no_proxy HTTP_PROXY HTTPS_PROXY FTP_PROXY NO_PROXY; do
    if [ -n "${!var}" ]; then
        PROXY_ARGS="$PROXY_ARGS --build-arg $var=${!var}"
        PROXY_RUN_ARGS="$PROXY_RUN_ARGS -e $var=${!var}"
    fi
done

echo "=========================================================="
echo "1. Building Base Docker Image: ${IMAGE_TAG}"
echo "=========================================================="
if docker image inspect ${IMAGE_TAG} > /dev/null 2>&1; then
    echo "✅ Image ${IMAGE_TAG} exists, skipping build"
else
    echo "🔨 Building image ${IMAGE_TAG}..."
    docker build --network=host -f Dockerfile.build -t ${IMAGE_TAG} .
fi

# Ensure the local persistent cache directory for Python versions exists on the host
mkdir -p .pyenv_versions

echo ""
echo "=========================================================="
echo "2. Compiling bindings and running auditwheel inside container"
echo "=========================================================="

# Run container with --rm.
# We mount:
#   - $(pwd) to /workspace/manus_package
#   - $(pwd)/.pyenv_versions to /root/.pyenv/versions (caching python versions permanently on host!)
docker run --network=host --rm \
    -v "$(pwd)":/workspace/manus_package \
    -v "$(pwd)/.pyenv_versions":/root/.pyenv/versions \
    -e PYTHON_VERSION=${PYTHON_VERSION} \
    ${PROXY_RUN_ARGS} \
    ${IMAGE_TAG} \
    bash -c "cd /workspace/manus_package && \
             echo 'Cleaning up previous builds...' && \
             rm -rf build/ dist/ *.egg-info && \
             echo 'Executing build_wheel.sh inside pyenv...' && \
             ./build_wheel.sh"

echo ""
echo "=========================================================="
echo "3. Build Completed successfully!"
echo "=========================================================="
echo "Your self-contained wheel packages are located in: manus_package/wheelhouse/"
ls -lh wheelhouse/
