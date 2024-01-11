// -----------------------------------------------------------------
// nuclear rng - generation 3
// Copyright (C) 2023,2024  Gabriele Bonacini
//
// This program is distributed under dual license:
// - Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0) License
// for non commercial use, the license has the following terms:
// * Attribution — You must give appropriate credit, provide a link to the license,
// and indicate if changes were made. You may do so in any reasonable manner,
// but not in any way that suggests the licensor endorses you or your use.
// * NonCommercial — You must not use the material for commercial purposes.
// A copy of the license it's available to the following address:
// http://creativecommons.org/licenses/by-nc/4.0/
// - For commercial use a specific license is available contacting the author.
// -----------------------------------------------------------------

#pragma once

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h" 
#include "pico/mutex.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/timer.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include <iostream>
#include <array>
#include <deque>
#include <utility>
#include <string>
#include <algorithm>
#include <limits>

namespace geigergen3 {

    using std::cerr,
          std::array,
          std::string,
          std::to_string,
          std::copy_n,
          std::deque,
          std::numeric_limits;

    class TimeStatistics{
        private:
             const uint64_t minute { 60'000'000 };
             uint64_t       start  { 0 },
                            stop   { 0 };
    
        public:
            void      setStartingTime(void)               noexcept; 
            void      setEndingTime(void)                 noexcept; 
            uint64_t  getExecutionTime(void)      const   noexcept;
            bool      isMinuteExpired(void)       const   noexcept;

            static uint64_t getElapsedTime(void)          noexcept;
            static uint64_t getMSec(uint64_t elapsed)     noexcept;
            static uint64_t getSec(uint64_t elapsed)      noexcept;
    };

    void TimeStatistics::setStartingTime(void) noexcept{
          start =  to_us_since_boot(get_absolute_time());
    }

    void TimeStatistics::setEndingTime(void) noexcept{
          stop  =  to_us_since_boot(get_absolute_time());
    }

    uint64_t TimeStatistics::getExecutionTime(void) const noexcept{
         return stop - start;
    }

    bool   TimeStatistics::isMinuteExpired(void) const   noexcept{
        if(TimeStatistics::getElapsedTime() - start >= minute ) return true;
        return false;
    }

    uint64_t TimeStatistics::getElapsedTime(void) noexcept{
         return to_us_since_boot(get_absolute_time());
    }

    uint64_t TimeStatistics::getMSec(uint64_t elapsed) noexcept{
          return elapsed / 1'000;
    }

    uint64_t TimeStatistics::getSec(uint64_t elapsed) noexcept{
          return elapsed / 1'000'000;
    }

    class DetectionLoopStats{
        private:
             TimeStatistics   stats;

             uint64_t         max         { 0 },
                              min         { numeric_limits<uint64_t>::max() },
                              last        { 0 };
             const size_t     UNDER_THR   { 3 },
                              ABOVE_THR   { 2500 };
             size_t           underAll    { 0 },
                              aboveAll    { 0 };
                              

        public:
             void      start(void)          noexcept;
             void      stop(void)           noexcept;

             uint64_t  getMax(void)   const noexcept;
             uint64_t  getMin(void)   const noexcept;
             uint64_t  getLast(void)  const noexcept;
             size_t    getUnder(void) const noexcept;
             size_t    getAbove(void) const noexcept;
    };

    void  DetectionLoopStats::start(void)    noexcept{
        stats.setStartingTime();
    }

    size_t DetectionLoopStats::getUnder(void) const noexcept{
        return underAll;
    }

    size_t DetectionLoopStats::getAbove(void) const noexcept{
        return aboveAll;
    }

    void  DetectionLoopStats::stop(void)     noexcept{
        stats.setEndingTime();
        last = stats.getExecutionTime();
        if(last >= UNDER_THR && last <= ABOVE_THR){
            if(max < last) max = last;
            if(min > last || min == 0) min = last;
        }else{
            if(last < UNDER_THR ) underAll++;
            if(last > ABOVE_THR ) aboveAll++;
        }
    }

    uint64_t DetectionLoopStats::getMax(void)  const noexcept{
         return max;
    }

    uint64_t DetectionLoopStats::getMin(void)  const noexcept{
         return min;
    }

    uint64_t DetectionLoopStats::getLast(void) const noexcept{
         return last;
    }

    class Cpm{
        private:
            unsigned int     cpm     { 0 },
                             cpmTmp  { 0 },
                             minutes { 0 };
            unsigned long    sumCpms { 0 };

            TimeStatistics   stats;

        public:
            void          start(void)                         noexcept;
            void          update(void)                        noexcept;
            unsigned int  getLastMinute(void)    const        noexcept;          
            unsigned long getAverage(void)       const        noexcept;          
    };

    void  Cpm::start(void) noexcept{
         stats.setStartingTime();
    }

    void  Cpm::update(void) noexcept{
         cpmTmp++;
         if(stats.isMinuteExpired()){
            sumCpms += cpmTmp;
            cpm      = cpmTmp;
            cpmTmp   = 0;
            minutes++;
            stats.setStartingTime();
         }
    }

    unsigned int  Cpm::getLastMinute(void) const noexcept {
         return cpm;
    }

    unsigned long Cpm::getAverage(void) const noexcept{
         return sumCpms / minutes;
    }

    using  rng=unsigned char;
    using  registry=unsigned int;
    static_assert(  numeric_limits<rng>::max() <  numeric_limits<registry>::max() ); 
    using  Rng=std::pair<rng, registry>;

    class GeigerGen3 {
        public:
            static inline constexpr unsigned int        MAX_RESULT           { 255 },
                                                        INVALID_RESULT       { MAX_RESULT + 1 };

            static_assert( INVALID_RESULT <  numeric_limits<registry>::max() ); 
            static_assert( MAX_RESULT < INVALID_RESULT ); 

            static GeigerGen3*     getInstance(unsigned int  pin, 
                                               unsigned int  vthr,
                                               unsigned int  zero)     noexcept;
            void                   init(void)                          noexcept;
            static void            abort(const char* msg)              noexcept;
            void                   detect(void)                        noexcept;
            static Rng             getRnd(void)                        noexcept;
            static size_t          getAvailable(void)                  noexcept;
            static string          getStats(void)                      noexcept;

            static inline Cpm                                      cpmStats;
            static inline DetectionLoopStats                       loopStats;

        private:
            static inline mutex_t                                  rndMutex;
            static inline deque<Rng>                               rndQueue; 
            static inline const size_t                             MAX_QUEUE_LEN        { 10240 };
            static inline long                                     count                { 0L },
                                                                   genCount             { 0L },
                                                                   lastCount            { 0L };
            static inline unsigned int                             gpioPin,
                                                                   vthreshold,
                                                                   zerothreshold;

            static inline GeigerGen3*                              instance             { nullptr };
            static inline unsigned int                             roulette             { 0 },
                                                                   lastRnd              { INVALID_RESULT };

            explicit GeigerGen3(unsigned int pin, 
                                unsigned int vhtr,
                                unsigned int zero)                 noexcept;
    };

    GeigerGen3::GeigerGen3(unsigned int pin, unsigned int  vthr, unsigned int zero)  noexcept {
        gpioPin       = pin;
        vthreshold    = vthr;
        zerothreshold = zero;
    }

    void  GeigerGen3::abort(const char* msg) noexcept{
        cerr << "Abort : " << msg << "\n";
        for(;;) sleep_ms(1000);
    }

    Rng GeigerGen3::getRnd(void) noexcept{
        Rng ret { INVALID_RESULT, 0 };
        if( ! GeigerGen3::rndQueue.empty()){
             mutex_enter_blocking(&GeigerGen3::rndMutex);
             ret = GeigerGen3::rndQueue.front();
             GeigerGen3::rndQueue.pop_front();
             mutex_exit(&GeigerGen3::rndMutex);
        }
        return ret;
    }

    size_t  GeigerGen3::getAvailable(void)  noexcept{
          return GeigerGen3::rndQueue.size();
    }

    void GeigerGen3::init(void)  noexcept {
        stdio_init_all(); 

        adc_init();
        adc_gpio_init(gpioPin);
        adc_select_input(0);

        mutex_init(&rndMutex);
    }

    GeigerGen3* GeigerGen3::getInstance(unsigned int pin, unsigned int vthr, unsigned int zero){
        if(instance == nullptr) instance = new GeigerGen3(pin, vthr, zero);
        return instance;
    }

    void GeigerGen3::detect(void)  noexcept{
        auto detectionThread = [](){ 
           GeigerGen3::cpmStats.start();
           for(;;){
               uint16_t result { adc_read() };
               GeigerGen3::loopStats.start();
               if(result > vthreshold){ 

                  mutex_enter_blocking(&GeigerGen3::rndMutex);
                  if(GeigerGen3::rndQueue.size() > GeigerGen3::MAX_QUEUE_LEN) GeigerGen3::rndQueue.pop_front();
                  GeigerGen3::rndQueue.push_back({GeigerGen3::roulette % (MAX_RESULT + 1), GeigerGen3::roulette});
                  mutex_exit(&GeigerGen3::rndMutex);

                  GeigerGen3::count++;
                  GeigerGen3::cpmStats.update();

                  for(;;){ result = adc_read();
                        if(result > zerothreshold ) sleep_us(10);
                        else  break;
                  }
               }
               GeigerGen3::roulette++;
               GeigerGen3::loopStats.stop();
           }
        };

        multicore_launch_core1(detectionThread);
    }

    string GeigerGen3::getStats(void) noexcept{

        return string("cpm:").append(to_string(GeigerGen3::cpmStats.getLastMinute()))
                             .append(":").append(to_string(GeigerGen3::cpmStats.getAverage()))
                             .append(":loop:").append(to_string(GeigerGen3::loopStats.getMin()))
                             .append(":").append(to_string(GeigerGen3::loopStats.getMax()))
                             .append(":").append(to_string(GeigerGen3::loopStats.getUnder()))
                             .append(":").append(to_string(GeigerGen3::loopStats.getAbove()));
    }

    using Pbuf=struct pbuf;
    using TcpPcb=struct tcp_pcb;
    static const  u16_t BUF_SIZE {2048};
    using Buffer=array<uint8_t,BUF_SIZE> ;
    struct Context {
        TcpPcb    *server_pcb,
                  *client_pcb;
        Buffer    bufferSend,
                  bufferRecv;
        u16_t     toSendLen,
                  sentLen,
                  recvLen;
    };

    class GeigerGen3NetworkLayer{
        public:
            explicit GeigerGen3NetworkLayer(u16_t port=6666)                                       noexcept;
            int      service(void)                                                                 noexcept;

        private:
            u16_t                   TCP_PORT;
            static  inline Context  context;

            static inline err_t clientClose(void *ctx)                                             noexcept;
            static inline err_t serverClose(void *ctx)                                             noexcept;
            static inline err_t serverResult(void *ctx, int status)                                noexcept;
            static inline err_t clientResult(void *ctx, int status)                                noexcept;
            static inline err_t result(void *ctx, int status)                                      noexcept;
            static inline err_t serverSentClbk(void *ctx, TcpPcb *tpcb, u16_t len)                 noexcept;
            static inline err_t serverSendData(void *ctx, TcpPcb *tpcb)                            noexcept;
            static inline err_t serverRecvClbk(void *ctx, TcpPcb *tpcb, Pbuf* pb, err_t err)       noexcept;
            static inline void  serverErrClbk(void *ctx, err_t err)                                noexcept;
            static inline err_t serverAccept(void *ctx, TcpPcb *client_pcb, err_t err)             noexcept;
    };

    GeigerGen3NetworkLayer::GeigerGen3NetworkLayer(u16_t port) noexcept
         :   TCP_PORT{port}
    {
        cerr << "Connected.\n\nStarting server at " << ip4addr_ntoa(netif_ip4_addr(netif_list))  << " on port " <<  TCP_PORT << '\n';
    }

    err_t GeigerGen3NetworkLayer::serverClose(void *ctx) noexcept{
    Context *context   { static_cast<Context*>(ctx)};
    err_t err          { ERR_OK };
    cerr << "ServerClose\n";
    if(context->server_pcb){
        tcp_arg(context->server_pcb, nullptr);
        tcp_close(context->server_pcb);
        context->server_pcb = nullptr;
    }
    return err;
}

err_t GeigerGen3NetworkLayer::clientClose(void *ctx) noexcept{
    Context *context   { static_cast<Context*>(ctx)};
    err_t err          { ERR_OK };
    cerr << "ClientClose\n";
    if(context->client_pcb != nullptr){
        tcp_arg(context->client_pcb,  nullptr);
        tcp_sent(context->client_pcb, nullptr);
        tcp_recv(context->client_pcb, nullptr);
        tcp_err(context->client_pcb,  nullptr);
        err = tcp_close(context->client_pcb);
        if (err != ERR_OK) {
            cerr << "ClientClose : Error: ClientClose : " <<  err << '\n';
            tcp_abort(context->client_pcb);
            err = ERR_ABRT;
        }
        context->client_pcb = nullptr;
    }
    return err;
}

err_t GeigerGen3NetworkLayer::serverResult(void *ctx, int status) noexcept{
    cerr << "ServerResult: ";
    ( status == 0 ) ? cerr << "success\n" : cerr << "failed: " << status << '\n';
    
    return serverClose(ctx);
}

err_t GeigerGen3NetworkLayer::clientResult(void *ctx, int status) noexcept{
    cerr << "ClientResult: ";
    ( status == 0 ) ? cerr << "success\n" : cerr << "failed: " << status << '\n';
    
    return clientClose(ctx);
}

err_t GeigerGen3NetworkLayer::result(void *ctx, int status) noexcept{
    err_t ret          { ERR_OK };
    cerr << "Result\n";
    if(serverClose(ctx) != ERR_OK) ret = ERR_ABRT;
    if(clientClose(ctx) != ERR_OK) ret = ERR_ABRT;
    return ret;
}

err_t GeigerGen3NetworkLayer::serverSentClbk(void *ctx, TcpPcb *tpcb, u16_t len) noexcept{
    Context *context { static_cast<Context*>(ctx)};
    cerr << "ServerSentClbk : bytes sent: " << len << '\n';
    context->sentLen += len;

    return ERR_OK;
}

err_t GeigerGen3NetworkLayer::serverSendData(void *ctx, TcpPcb *tpcb)  noexcept{
    Context            *context { static_cast<Context*>(ctx)};

    context->sentLen = 0;
    cerr << "ServerSendData : writing " << context->toSendLen << " bytes to client\n";
    cyw43_arch_lwip_check();
    if(err_t err { tcp_write(tpcb, context->bufferSend.data(), context->toSendLen, TCP_WRITE_FLAG_COPY) }; err != ERR_OK){
        cerr << "ServerSendData : Error writing data : " <<  err << '\n';
        return clientResult(context, -1);
    }
    tcp_output(tpcb);  
    return ERR_OK;
}

err_t GeigerGen3NetworkLayer::serverRecvClbk(void *ctx, TcpPcb *tpcb, Pbuf* pb, err_t err)  noexcept{
    Context *context { static_cast<Context*>(ctx)};
    cerr << "ServerRecvClbk\n";
    if(!pb) return clientResult(context, -1);
    cyw43_arch_lwip_check();

    context->recvLen    =   pbuf_copy_partial(pb, context->bufferRecv.data(), pb->tot_len, 0);
    cerr << "ServerRecvClbk " <<  pb->tot_len << "/" <<  context->recvLen << " err " << static_cast<int>(err) << '\n';

    tcp_recved(tpcb, pb->tot_len);
    pbuf_free(pb);
    
    for(u16_t i{0}; i<context->recvLen; i=i+3){
        cerr << "ServerRecvClbk : Interation : " << ( (3 + i ) /3 )  << " of " << context->recvLen / 3
             << " payload: " <<  context->bufferRecv.at(0 + i) << " - "
             <<  context->bufferRecv.at(1 + i) << " - "
             <<  context->bufferRecv.at(2 + i) << '\n';
    
        auto ckeckReq = [&]() -> int  {   if(     context->bufferRecv.at(0 + i) == 'r' && 
                                                  context->bufferRecv.at(1 + i) == 'e' && 
                                                  context->bufferRecv.at(2 + i) == 'q'  )  return 0;
                                          else if(context->bufferRecv.at(0 + i) == 'e' && 
                                                  context->bufferRecv.at(1 + i) == 'n' && 
                                                  context->bufferRecv.at(2 + i) == 'd'  )  return 1;
                                          else if(context->bufferRecv.at(0 + i) == 's' && 
                                                  context->bufferRecv.at(1 + i) == 't' && 
                                                  context->bufferRecv.at(2 + i) == 'a'  )  return 2;
                                          else                                             return 3;
                                       };
        int par { ckeckReq() };
        cerr << "ServerRecvClbk: detect type : " << par  <<'\n';
        err_t err { ERR_OK };
        switch(par){
            case 0:
                {
                    cerr << "ServerRecvClbk: send for req\n";
                    Rng                rndn     { GeigerGen3::getRnd() };
                    string             msg      { to_string(rndn.first).append(":").append(to_string(rndn.second)).append(":")
                                                                       .append(to_string(GeigerGen3::getAvailable())).append("\n") };
        
                    context->toSendLen = msg.size() <= context->bufferSend.size() ? msg.size() : context->bufferSend.size();
                    copy_n(msg.data(),  context->toSendLen, context->bufferSend.data());
                    err =  serverSendData(context, context->client_pcb);
                }
            break;
            case 1:
                    cerr << "ServerRecvClbk: close for end\n";
                    err = clientClose(context);
            break;
            case 2:
                {
                    cerr << "ServerRecvClbk: statistics\n";
                    string             stats    { GeigerGen3::getStats() };
        
                    context->toSendLen = stats.size() <= context->bufferSend.size() ? stats.size() : context->bufferSend.size();
                    copy_n(stats.data(),  context->toSendLen, context->bufferSend.data());
                    err = serverSendData(context, context->client_pcb);
                }
            break;
            default:
                    cerr << "ServerRecvClbk: error\n";
                    err = clientClose(context); 
        } 
    } 

    cerr << "ServerRecvClbk : end \n";
    return err;
}

void GeigerGen3NetworkLayer::serverErrClbk(void *ctx, err_t err)  noexcept{
    cerr << "ServerErrClbk\n";
    if(err != ERR_ABRT) {
        cerr << "ServerErrClbk : " << err << '\n';
        serverResult(ctx, err);
    }
}
  
err_t GeigerGen3NetworkLayer::serverAccept(void *ctx, TcpPcb *client_pcb, err_t err)  noexcept{
    Context *context { static_cast<Context*>(ctx)};
    cerr << "ServerErrClbk\n";
    if(err != ERR_OK || client_pcb == nullptr) {
        cerr << "ServerErrClbk: Error: accept\n";
        clientResult(context, err);
        return ERR_VAL;
    }
    cerr << "ServerErrClbk: Client connected\n";

    context->client_pcb = client_pcb;
    tcp_arg(client_pcb, context);
    tcp_sent(client_pcb, serverSentClbk);
    tcp_recv(client_pcb, serverRecvClbk);
    tcp_err(client_pcb, serverErrClbk);

    string             msg      { "ready\n" };
    context->toSendLen = msg.size() <= context->bufferSend.size() ? msg.size() : context->bufferSend.size();
    copy_n(msg.data(),  context->toSendLen, context->bufferSend.data());
    return serverSendData(context, context->client_pcb);
}

int GeigerGen3NetworkLayer::service(void) noexcept{
    cerr << "Service\n";
    TcpPcb *pcb { tcp_new_ip_type(IPADDR_TYPE_ANY) };
    if(!pcb){
        cerr << "Service : Error: pcb creation\n";
        serverResult(&context, -1);
        return 1;
    }
    ip_set_option(pcb, SOF_REUSEADDR); 

    if(err_t err { tcp_bind(pcb, nullptr, TCP_PORT) }; err != ERR_OK){
        cerr << "Service : Error: bind to port : " <<  TCP_PORT << '\n';
        serverResult(&context, -1);
        return 1;
    }

    context.server_pcb = tcp_listen_with_backlog(pcb, 1);
    if(!context.server_pcb) {
        cerr <<  "Service : Error: listen\n";
        if(pcb) tcp_close(pcb);
        serverResult(&context, -1);
        return 1;
    }
    tcp_arg(context.server_pcb, &context);

    for(;;){
        tcp_accept(context.server_pcb, serverAccept);
        sleep_ms(50);
    }
}

} // End namespace