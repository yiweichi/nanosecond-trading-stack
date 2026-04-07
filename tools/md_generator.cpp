#include "nts/market_data.h"
#include "nts/instrument/clock.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <random>

static volatile sig_atomic_t running = 1;

static void on_signal(int) { running = 0; }

int main(int argc, char* argv[]) {
    uint16_t port = nts::DEFAULT_PORT;
    int      rate = 1000;   // messages per second

    if (argc > 1) port = static_cast<uint16_t>(std::atoi(argv[1]));
    if (argc > 2) rate = std::atoi(argv[2]);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in dest;
    std::memset(&dest, 0, sizeof(dest));
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(port);
    dest.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Deterministic random walk for reproducibility
    std::mt19937 rng(42);
    std::normal_distribution<double>         price_step(0.0, 0.01);
    std::uniform_int_distribution<uint32_t>  size_dist(100, 1000);

    double   price   = 100.0;
    double   spread  = 0.02;
    uint32_t seq     = 0;
    long     interval_us = 1'000'000L / rate;

    fprintf(stderr, "[md_generator] sending to 127.0.0.1:%u at %d msg/s\n",
            port, rate);

    while (running) {
        price += price_step(rng);

        nts::MdMsg msg;
        msg.timestamp_ns  = nts::instrument::now_ns();
        msg.instrument_id = nts::DEFAULT_INSTRUMENT;
        msg.sequence_num  = seq++;
        msg.bid_price     = price - spread * 0.5;
        msg.ask_price     = price + spread * 0.5;
        msg.bid_size      = size_dist(rng);
        msg.ask_size      = size_dist(rng);

        ssize_t sent = sendto(sockfd, &msg, sizeof(msg), 0,
                              reinterpret_cast<struct sockaddr*>(&dest),
                              sizeof(dest));
        if (sent < 0) {
            perror("sendto");
            break;
        }

        usleep(static_cast<useconds_t>(interval_us));
    }

    ::close(sockfd);
    fprintf(stderr, "[md_generator] sent %u messages\n", seq);
    return 0;
}
