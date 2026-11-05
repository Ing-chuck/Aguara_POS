# Source this: sets PYTHON to a command that runs Python 3, empty if there's none.
# The name "python" proves nothing: Qt's MinGW 8.1 brings a Python 2.7 of its own.
PYTHON=
for candidate in python3 python "py -3"; do
    if $candidate -c 'import sys; sys.exit(sys.version_info[0] != 3)' > /dev/null 2>&1; then
        PYTHON="$candidate"
        break
    fi
done
