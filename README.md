# Vintage2D

Dużo bazowałem na ubiegłorocznej wzorcówce. Przekazuję urządzeniu przez DMA tablicę stron przy podmianie canvasa oraz
przez bufor w DMA przekazuję komendy (na końcu bufora trzymam JUMP na początek). Stan tej kolejki symuluję też wewnątrz
sterownika - po każdej paczce (poprawne ustawenia, fill/blit) wstawiam komendę COUNTER z monotnicznym licznikiem
pozwalającym mi na zorientowanie się ile pozycji zostało już z kolejki zdjęte (śledzę stan licznika na podstawie diffa
z najnowszego odczytanego i poprzedniego modulo 2^24, w ten sposób unikam problemów z przekręceniem). Licznik pozwala mi
też realizować fsync (odwieszam odpowiednie kolejki). Licznik pobieram kiedy przyjdzie do mnie wyzwolone przez niego przerwanie NOTIFY.

Jeśli nie ma miejsca na write w kolejce, wieszam się na kolejce oczekiwania dla zapisu do kolejki. Tę kolejkę odwieszam, jeśli dostanę NOTIFY ze zmianą countera i wiem, że wykonałem chociaż jedną komendę (odwieszam po przesunięciu lokalnej głowy kolejki).

Ogon kolejki na karcie (WRITE_PTR) synchronizuję z moim ogonem. Głowę mojej kolejki, synchronizuję z głową na urządzeniu za pomocą wspomnianych liczników.

Zapewniam cache koherencę korzystając z bezpiecznych funkcji dma do alokacji bloków pamięci.