/// Full audio+video player demo

#include "api/player.h"
#include "api/player_callback.h"

#include <csignal>
#include <iostream>
#include <thread>

using namespace player;
using namespace std::chrono_literals;

volatile sig_atomic_t g_run=1;
void onSig(int){g_run=0;}

int main(int argc,char**argv){
  if(argc<2){std::cerr<<"Usage: "<<argv[0]<<" <file>\n";return 1;}
  signal(SIGINT,onSig);signal(SIGTERM,onSig);

  auto cfg=PlayerConfig::localFilePreset();
  auto p=IPlayer::create(cfg);

  struct CB:IPlayerCallback{
    void onStateChanged(PlayerState o,PlayerState n)override{
      const char* names[]={"Idle","Loading","Ready","Playing","Paused","Buffering","Completed","Error","Stopping"};
      std::cout<<"[state] "<<names[(int)o]<<" -> "<<names[(int)n]<<"\n";
    }
    void onError(ErrorCode,const char*m)override{std::cerr<<"[err] "<<m<<"\n";g_run=0;}
    void onCompletion()override{std::cout<<"[done]\n";g_run=0;}
  }cb;

  p->setCallback(&cb);
  std::cout<<"Open: "<<argv[1]<<"\n";
  if(p->open(argv[1])!=0){std::cerr<<"Failed\n";return 1;}
  std::cout<<"Play...\n"; p->play();

  while(g_run){
    auto s=p->getState();
    if(s==PlayerState::Completed||s==PlayerState::Error)break;
    std::this_thread::sleep_for(100ms);
  }
  p->stop();
  std::cout<<"Done.\n";
}
