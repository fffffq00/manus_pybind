#!/bin/bash
# Stop script on first error
set -e

# Setup pyenv path and shims
export PYENV_ROOT="/root/.pyenv"
export PATH="$PYENV_ROOT/bin:$PATH"

# Initialize pyenv shell integration
eval "$(pyenv init -)"

# Fallback to python 3.13 if PYTHON_VERSION is not provided
TARGET_VERSION="${PYTHON_VERSION:-3.13}"
echo "Target Python version requested: ${TARGET_VERSION}"

# Resolve the exact installed version if any matches
ACTIVE_VERSION=$(pyenv versions --bare | grep "^${TARGET_VERSION}\(\.\|$\)" | tail -n 1)

if [ -z "$ACTIVE_VERSION" ]; then
    echo "Python ${TARGET_VERSION} is not installed inside the container's pyenv."
    echo "Starting compilation of Python ${TARGET_VERSION} from source (this might take a few minutes for the first run)..."
    pyenv install "${TARGET_VERSION}"
    ACTIVE_VERSION=$(pyenv versions --bare | grep "^${TARGET_VERSION}\(\.\|$\)" | tail -n 1)
    echo "Python ${ACTIVE_VERSION} successfully installed!"
else
    echo "Python ${ACTIVE_VERSION} is already installed in pyenv (cached)."
fi

# Set the active Python version using the exact resolved version
pyenv shell "${ACTIVE_VERSION}"

# Double check that we are running the correct python binary
echo "Active python binary: $(which python)"
python --version

echo "=== Installing/Upgrading build system dependencies inside pyenv ==="
pip install --upgrade pip
pip install build pybind11 wheel auditwheel patchelf

echo "=== Compiling and building Python Wheel (.whl) ==="
python -m build --wheel

MAJOR_VERSION=$(echo "$ACTIVE_VERSION" | cut -d. -f1)
MINOR_VERSION=$(echo "$ACTIVE_VERSION" | cut -d. -f2)

echo "=== Repairing wheel with auditwheel to bundle dependencies ==="
if [ "$MAJOR_VERSION" -eq 3 ] && [ "$MINOR_VERSION" -le 8 ]; then
    echo "Detected Python ${ACTIVE_VERSION} (<= 3.8). Specifying modern plat tag for auditwheel..."
    auditwheel repair --plat manylinux_2_31_x86_64 dist/manus_pybind-0.1.0-*.whl
else
    echo "Detected Python ${ACTIVE_VERSION} (>= 3.9). Using default auditwheel behavior..."
    auditwheel repair dist/manus_pybind-0.1.0-*.whl
fi

echo ""
echo "=== Build finished! ==="
