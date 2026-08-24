#!/usr/bin/env bash
# Catches Qt idioms that compile cleanly and fail at runtime. Added after a
# QMessageBox::exec() return value was treated as an index, which silently broke
# the "Reset" button on Linux while working on Windows (upstream #1802).
set -uo pipefail

cd "$(dirname "$0")/.."
status=0
sources=(src include)

report() {
    local level=$1 message=$2
    shift 2
    [[ $level == error ]] && status=1
    echo "$level: $message"
    printf '  %s\n' "$@"
}

scan() {
    grep -rn --include='*.cpp' --include='*.hpp' -E -e "$1" "${sources[@]}" || true
}

# exec() returns an opaque value when the dialog carries custom buttons; only
# clickedButton() says which one was pressed.
mapfile -t hits < <(scan '\bexec\(\)[[:space:]]*[-+][[:space:]]*[0-9]')
((${#hits[@]})) && report error "arithmetic on QDialog::exec(); use clickedButton() for custom buttons" "${hits[@]}"

# Calling a signal reads as a method call and hides that it fans out to slots.
mapfile -t hits < <(scan '\->(stateChanged|checkStateChanged|clicked|triggered)\(' | grep -v 'connect(' || true)
((${#hits[@]})) && report warning "signal invoked directly; prefer setChecked()/click() or call the slot" "${hits[@]}"

# Deprecated in Qt 6.9. Not an error: this project still builds against system Qt
# packages older than that, where the replacement does not exist.
mapfile -t hits < <(scan 'QCheckBox::stateChanged')
((${#hits[@]})) && report warning "QCheckBox::stateChanged is deprecated since Qt 6.9 (revisit when the minimum Qt moves past it)" "${hits[@]}"

((status)) && echo && echo "Qt idiom lint failed."
((status)) || echo "Qt idiom lint passed."
exit $status
