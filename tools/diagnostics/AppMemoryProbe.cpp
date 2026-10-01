#include <libproc.h>
#include <sys/resource.h>
#include <chrono>
#include <thread>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    const int pid=std::stoi(argv[1]), seconds=std::stoi(argv[2]);
    if(pid<=0 || seconds<1 || seconds>60) return 2;
    for(int i=0;i<seconds;++i) {
        rusage_info_v4 usage{};
        if(proc_pid_rusage(pid,RUSAGE_INFO_V4,reinterpret_cast<rusage_info_t*>(&usage))) return 1;
        std::cout << "{\"pid\":" << pid << ",\"second\":" << i
            << ",\"physicalFootprintBytes\":" << usage.ri_phys_footprint
            << ",\"residentBytes\":" << usage.ri_resident_size
            << ",\"peakPhysicalFootprintBytes\":" << usage.ri_lifetime_max_phys_footprint
            << ",\"cpuNanoseconds\":" << usage.ri_user_time+usage.ri_system_time << "}" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
