Sposób użycia: make
Można też osobno skompilować testy: make all
Jak również uruchomić: make test



Przykład poprawnego wykonania testów:
    # make test
    diff -q <(./test-simple-output) <(./test-simple)
    diff -q <(./test-invalid-output) <(./test-invalid)
    diff -q <(./test-mmap-later-output) <(./test-mmap-later)
    diff -q <(./test-large-canvas-output) <(./test-large-canvas)
    diff -q <(./test-long-queue-output) <(./test-long-queue)
    diff -q <(./test-multi-context-output) <(./test-multi-context)
    diff -q <(./test-big-writes-output) <(./test-big-writes)
    diff -q <(./test-multi-device-output) <(./test-multi-device)

Test "test-multi-device" wymaga aby w systemie były co najmniej dwa urządzenia vintage2d.

Za każdy z testów można dostać 0.625 punktów.
