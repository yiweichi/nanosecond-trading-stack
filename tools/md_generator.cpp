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
#include <random>

static volatile sig_atomic_t running = 1;

static void on_signal(int) { running = 0; }

int main(int argc, char* argv[]) {
    uint16_t port        = nts::DEFAULT_PORT;
    int      rate        = 1000;
    bool     send_depth  = false;
    bool     send_trades = false;

    if (argc > 1) port        = static_cast<uint16_t>(std::atoi(argv[1]));
    if (argc > 2) rate        = std::atoi(argv[2]);
    if (argc > 3) send_depth  = (std::atoi(argv[3]) != 0);
    if (argc > 4) send_trades = (std::atoi(argv[4]) != 0);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }

    struct sockaddr_in dest;
    std::memset(&dest, 0, sizeof(dest));
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(port);
    dest.sin_addr.s_addr = inet_addr("127.0.0.1");

    std::mt19937 rng(42);
    std::normal_distribution<double>         price_step(0.0, 0.01);
    std::uniform_int_distribution<uint32_t>  size_dist(100, 1000);
    std::uniform_int_distribution<uint32_t>  count_dist(1, 20);

    double   price   = 100.0;
    double   spread  = 0.02;
    uint32_t seq     = 0;
    long     interval_us = 1'000'000L / rate;
    uint64_t msg_count   = 0;

    fprintf(stderr, "[md_generator] sending to 127.0.0.1:%u at %d msg/s", port, rate);
    if (send_depth)  fprintf(stderr, " +depth");
    if (send_trades) fprintf(stderr, " +trades");
    fprintf(stderr, "\n");

    while (running) {
        price += price_step(rng);

        // ── L1 quote ─────────────────────────────────────────────
        nts::MdQuote q;
        std::memset(&q, 0, sizeof(q));
        q.header.timestamp_ns  = nts::instrument::now_ns();
        q.header.instrument_id = nts::DEFAULT_INSTRUMENT;
        q.header.sequence_num  = seq++;
        q.header.type          = nts::MdMsgType::Quote;
        q.bid_price = price - spread * 0.5;
        q.ask_price = price + spread * 0.5;
        q.bid_size  = size_dist(rng);
        q.ask_size  = size_dist(rng);

        sendto(sockfd, &q, sizeof(q), 0,
               reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
        msg_count++;

        // ── L2 depth (every 5th tick) ────────────────────────────
        if (send_depth && (seq % 5 == 0)) {
            nts::MdDepth d;
            std::memset(&d, 0, sizeof(d));
            d.header.timestamp_ns  = nts::instrument::now_ns();
            d.header.instrument_id = nts::DEFAULT_INSTRUMENT;
            d.header.sequence_num  = seq++;
            d.header.type          = nts::MdMsgType::Depth;
            d.bid_levels = 5;
            d.ask_levels = 5;
            for (int i = 0; i < 5; i++) {
                d.bids[i].price       = price - spread * 0.5 - i * 0.01;
                d.bids[i].size        = size_dist(rng);
                d.bids[i].order_count = count_dist(rng);
                d.asks[i].price       = price + spread * 0.5 + i * 0.01;
                d.asks[i].size        = size_dist(rng);
                d.asks[i].order_count = count_dist(rng);
            }
            sendto(sockfd, &d, sizeof(d), 0,
                   reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
            msg_count++;
        }

        // ── Trade tick (every 10th tick) ─────────────────────────
        if (send_trades && (seq % 10 == 0)) {
            nts::MdTrade t;
            std::memset(&t, 0, sizeof(t));
            t.header.timestamp_ns  = nts::instrument::now_ns();
            t.header.instrument_id = nts::DEFAULT_INSTRUMENT;
            t.header.sequence_num  = seq++;
            t.header.type          = nts::MdMsgType::Trade;
            t.price          = price;
            t.size           = size_dist(rng);
            t.aggressor_side = (price_step(rng) > 0) ? nts::Side::Buy : nts::Side::Sell;

            sendto(sockfd, &t, sizeof(t), 0,
                   reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
            msg_count++;
        }

        usleep(static_cast<useconds_t>(interval_us));
    }

    ::close(sockfd);
    fprintf(stderr, "[md_generator] sent %llu messages (%u sequences)\n",
            static_cast<unsigned long long>(msg_count), seq);
    return 0;
}
