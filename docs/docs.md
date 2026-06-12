# Alarm System - Dokumentacja projektu


## 1. Opis projektu

Projekt implementuje system alarmu przeciwwłamaniowego. Składa się z serwera i klientów, gdzie każdy klient to osobne urządzenie np. czujnik ruchu. Jak klient wykryje zagrożenie to wysyła alarm do serwera, który wyświetla powiadomienie i odtwarza dźwięk. Komunikacja odbywa się przez TCP.

---

## 2. Architektura

W skład systemu wchodzą dwa programy. Pierwszy to server_app, czyli serwer uruchamiany jeden raz, który zarządza listą urządzeń i odbiera alarmy. Drugi to client_app, uruchamiany oddzielnie dla każdego urządzenia.

Działanie wygląda tak:

1. Na serwerze dodajemy nazwy urządzeń które mogą się łączyć, potem go uruchamiamy.
2. Klient podaje swoją nazwę i adres serwera i próbuje się połączyć.
3. Serwer sprawdza czy ta nazwa jest na liście, jak jest to akceptuje połączenie.
4. Połączony klient może wysyłać alarmy.

---

## 3. Protokół komunikacyjny

Napisałem własny prosty protokół tekstowy. Każda wiadomość to jedna linia w formacie:

```
TYP|nazwa_urzadzenia|tekst
```

Przykład: `ALARM|czujnik-salon|wykryto ruch`

Wiadomości używane w protokole:

| Wiadomość | Kierunek | Opis |
|---|---|---|
| `REGISTER\|nazwa\|` | klient -> serwer | klient podaje swoją nazwę |
| `OK\|nazwa\|` | serwer -> klient | nazwa zaakceptowana |
| `DENIED\|nazwa\|powód` | serwer -> klient | nazwa odrzucona |
| `ALARM\|nazwa\|treść` | klient -> serwer | wysłanie alarmu |
| `ACK\|nazwa\|` | serwer -> klient | potwierdzenie alarmu |
| `PING\|\|` | klient -> serwer | sprawdzenie czy serwer żyje |
| `PONG\|\|` | serwer -> klient | odpowiedź na ping |
| `DISCONNECT\|nazwa\|` | klient -> serwer | rozłączenie |

---

## 4. Użyte konstrukcje z zajęć

### 4.1 Wątki

Serwer tworzy osobny wątek dla każdego klienta który się połączy. Dzięki temu może obsługiwać kilka urządzeń naraz bo wątki działają równolegle.

```c
pthread_t tid;
pthread_attr_t attr;
pthread_attr_init(&attr);
pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
pthread_create(&tid, &attr, client_thread, ca);
```

Wątki są tworzone jako detached bo serwer nie musi czekać aż skończą działanie, system sam zwolni pamięć.

Po stronie klienta też jest wątek w tle (recv_thread) który czeka na wiadomości od serwera (ACK, PONG). Bez tego główny wątek blokował by się na recv() i program nie reagowałby na kliknięcia.

### 4.2 Sygnały

Kiedy klient próbuje wysłać coś przez socket do serwera który już nie działa, system wysyła sygnał SIGPIPE który normalnie zabija program. Żeby temu zapobiec używam flagi MSG_NOSIGNAL przy każdym send():

```c
int n = send(fd, buf, strlen(buf), MSG_NOSIGNAL);
if (n <= 0) {
    ctx->running = 0;
    return -1;
}
```

Zamiast crasha program dostaje kod błędu i może się normalnie rozłączyć.

### 4.3 Pamięć współdzielona

Lista urządzeń (DeviceRegistry) jest dostępna ze wszystkich wątków serwera jednocześnie. Żeby dwa wątki nie próbowały jej zmieniać w tym samym momencie, chronię ją mutexem:

```c
typedef struct {
    Device list[MAX_DEVICES];
    int count;
    pthread_mutex_t lock;
} DeviceRegistry;
```

Każda funkcja która coś na tej liście robi, najpierw blokuje mutex:

```c
pthread_mutex_lock(&reg->lock);
// operacja na liście
pthread_mutex_unlock(&reg->lock);
```

Napisałem też funkcję devices_snapshot() która kopiuje całą listę pod mutexem do osobnej tablicy, żeby GUI mogło ją potem rysować już bez trzymania blokady.

### 4.4 Mutex

Mutex jest potrzebny bo bez niego dwa wątki mogłyby jednocześnie wpisywać coś do tej samej tablicy i dane by się pomieszały. Inicjalizacja i niszczenie:

```c
pthread_mutex_init(&reg->lock, NULL);
// ...
pthread_mutex_destroy(&reg->lock);
```

### 4.5 Sockety TCP

Serwer otwiera socket TCP, binduje go do portu i czeka na połączenia. Dodałem SO_REUSEADDR i SO_REUSEPORT żeby można było restartować serwer od razu po wyłączeniu, bez czekania:

```c
int opt = 1;
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
bind(fd, (struct sockaddr *)&addr, sizeof(addr));
listen(fd, 8);
```

Klient łączy się przez connect(), wysyła REGISTER i czeka na odpowiedź OK albo DENIED.

### 4.6 Make

Do budowania projektu używam Makefile. Są dwa cele, server i client, kompilowane osobno:

```makefile
CC     = gcc
CFLAGS = -Wall -Wextra -g $(shell pkg-config --cflags gtk+-3.0)
LIBS   = $(shell pkg-config --libs gtk+-3.0) -lpthread

all: ../server client

server: $(SERVER_SRC)
	$(CC) $(CFLAGS) -o server_app $(SERVER_SRC) $(LIBS)

client: $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o client_app $(CLIENT_SRC) $(LIBS)
```

Żeby skompilować cały projekt wystarczy wpisać make. make clean usuwa skompilowane pliki.

---

## 5. Struktura plików

| Plik | Opis |
|---|---|
| `common/protocol.h/c` | typy wiadomości i funkcje encode/decode |
| `server/devices.h/c` | lista urządzeń z mutexem |
| `server/server_net.h/c` | socket serwera i wątki klientów |
| `server/main.c` | okno serwera |
| `client/client_net.h/c` | połączenie z serwerem i wysyłanie alarmów |
| `client/main.c` | okno klienta |
| `Makefile` | kompilacja |

---

## 6. Kompilacja i uruchomienie

```bash
make

./server_app
./client_app
./client_app
```
