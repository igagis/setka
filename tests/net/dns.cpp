#include "dns.hpp"

#include "../../src/setka/HostNameResolver.hpp"

#include <nitki/Thread.hpp>
#include <nitki/semaphore.hpp>

#include <memory>
#include <vector>

namespace TestSimpleDNSLookup{

class Resolver : public setka::HostNameResolver{
	
public:
	
	Resolver(nitki::semaphore& sema, const std::string& hostName = std::string()) :
			sema(sema),
			hostName(hostName)
	{}
	
	setka::ip_address::ip ip;
	
	nitki::semaphore& sema;
	
	E_Result result;
	
	std::string hostName;
	
	void Resolve(){
		this->resolve_ts(this->hostName, 10000);
	}
	
	void onCompleted_ts(E_Result result, setka::ip_address::ip ip)noexcept override{
		TRACE(<< "onCompleted_ts(): result = " << unsigned(result) << " ip = " << ip.to_string() << std::endl)
		
//		ASSERT_INFO_ALWAYS(result == ting::net::HostNameResolver::OK, "result = " << result)
		this->result = result;
		
		this->ip = ip;
		
		this->sema.signal();
	}
};

void Run(){
	{//test one resolve at a time
		nitki::semaphore sema;

		Resolver r(sema);

		r.resolve_ts("google.com", 10000);

		TRACE(<< "TestSimpleDNSLookup::Run(): waiting on semaphore" << std::endl)
		
		if(!sema.wait(11000)){
			ASSERT_ALWAYS(false)
		}

		ASSERT_INFO_ALWAYS(r.result == setka::HostNameResolver::E_Result::OK, "r.result = " << unsigned(r.result))

//		ASSERT_INFO_ALWAYS(r.ip == 0x4D581503 || r.ip == 0x57FAFB03, "r.ip = " << r.ip)
		ASSERT_INFO_ALWAYS(r.ip.is_valid(), "ip = " << r.ip.to_string())

		TRACE(<< "ip = " << r.ip.to_string() << std::endl)
	}
	
	{//test several resolves at a time
		nitki::semaphore sema;

		typedef std::vector<std::unique_ptr<Resolver> > T_ResolverList;
		typedef T_ResolverList::iterator T_ResolverIter;
		T_ResolverList r;

		r.push_back(std::unique_ptr<Resolver>(new Resolver(sema, "google.ru")));
		r.push_back(std::unique_ptr<Resolver>(new Resolver(sema, "ya.ru")));
		r.push_back(std::unique_ptr<Resolver>(new Resolver(sema, "mail.ru")));
		r.push_back(std::unique_ptr<Resolver>(new Resolver(sema, "vk.com")));
		
//		TRACE(<< "starting resolutions" << std::endl)
		
		for(T_ResolverIter i = r.begin(); i != r.end(); ++i){
			(*i)->Resolve();
		}
		
		for(unsigned i = 0; i < r.size(); ++i){
			if(!sema.wait(11000)){
				ASSERT_ALWAYS(false)
			}
		}
//		TRACE(<< "resolutions done" << std::endl)
		
		for(T_ResolverIter i = r.begin(); i != r.end(); ++i){
			ASSERT_INFO_ALWAYS((*i)->result == setka::HostNameResolver::E_Result::OK, "result = " << unsigned((*i)->result) << " host to resolve = " << (*i)->hostName)
//			ASSERT_INFO_ALWAYS((*i)->ip == 0x4D581503 || (*i)->ip == 0x57FAFB03, "(*i)->ip = " << (*i)->ip)
			ASSERT_ALWAYS((*i)->ip.is_valid())
		}
	}
}

}



namespace TestRequestFromCallback{

class Resolver : public setka::HostNameResolver{
	
public:
	
	Resolver(nitki::semaphore& sema) :
			sema(sema)
	{}
	
	std::string host;
	
	setka::ip_address::ip ip;
	
	nitki::semaphore& sema;
	
	E_Result result;
	

	void onCompleted_ts(E_Result result, setka::ip_address::ip ip)noexcept override{
//		ASSERT_INFO_ALWAYS(result == ting::net::HostNameResolver::OK, "result = " << result)
		
		if(this->host.size() == 0){
			ASSERT_INFO_ALWAYS(result == setka::HostNameResolver::E_Result::NO_SUCH_HOST, "result = " << unsigned(result))
			ASSERT_ALWAYS(!ip.is_valid())
			
			this->host = "ya.ru";
			this->resolve_ts(this->host, 5000);
		}else{
			ASSERT_ALWAYS(this->host == "ya.ru")
			this->result = result;
			this->ip = ip;
			this->sema.signal();
		}
	}
};

void Run(){
	nitki::semaphore sema;
	
	Resolver r(sema);
	
	r.resolve_ts("rfesfdf.ru", 3000);
	
	if(!sema.wait(8000)){
		ASSERT_ALWAYS(false)
	}
	
	ASSERT_INFO_ALWAYS(r.result == setka::HostNameResolver::E_Result::OK, "r.result = " << unsigned(r.result))

//	ASSERT_INFO_ALWAYS(r.ip == 0x4D581503 || r.ip == 0x57FAFB03, "r.ip = " << r.ip)
	ASSERT_ALWAYS(r.ip.is_valid())
}
}



namespace TestCancelDNSLookup{
class Resolver : public setka::HostNameResolver{
	
public:
	
	Resolver(){}
	
	volatile bool called = false;
	
	void onCompleted_ts(E_Result result, setka::ip_address::ip ip)noexcept override{
		this->called = true;
	}
};

void Run(){
	TRACE_ALWAYS(<< "\tRunning 'cacnel DNS lookup' test, it will take about 4 seconds" << std::endl)
	Resolver r;
	
	r.resolve_ts("rfesweefdqfdf.ru", 3000, setka::ip_address("1.2.3.4", 53));
	
	nitki::Thread::sleep(500);
	
	bool res = r.cancel_ts();
	
	nitki::Thread::sleep(3000);
	
	ASSERT_ALWAYS(res)
	
	ASSERT_ALWAYS(!r.called)
}
}//~namespace
