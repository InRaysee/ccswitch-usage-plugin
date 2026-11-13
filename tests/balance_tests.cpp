#include "ProviderBalance.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <windows.h>
using namespace balance;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    try {
        Provider p;
        p.code = R"(({request:{url:"{{baseUrl}}/usage",method:"GET",headers:{Authorization:"Bearer {{apiKey}}"}},extractor:r=>({remaining:r?.quota?.remaining ?? r?.balance,unit:r?.unit ?? "USD",isValid:r?.valid ?? true})}))";
        p.baseUrl = "https://example.invalid";
        p.apiKey = "synthetic-key'\"\\test";
        Request request;
        Value value;
        std::wstring error;
        if (argc == 3 && std::string(argv[1]) == "--http") {
            p.baseUrl = argv[2];
            Config config; config.proxyMode = "direct";
            Check(Fetch(p,config,value,error), "loopback GET failed");
            Check(value.remaining == 42.5, "GET balance mismatch");
            p.code = R"(({request:{url:"{{baseUrl}}/post",method:"POST",body:'{"test":true}',headers:{"Content-Type":"application/json"}},extractor:r=>({remaining:r.balance,unit:"USD"})}))";
            Check(Fetch(p,config,value,error) && value.remaining == 7, "loopback POST failed");
            p.code = R"(({request:{url:"{{baseUrl}}/denied",method:"GET"},extractor:r=>r}))";
            Check(!Fetch(p,config,value,error) && error.find(L"HTTP 403") != std::wstring::npos, "HTTP errors must propagate");
            p.code = R"(({request:{url:"{{baseUrl}}/redirect",method:"GET",headers:{Authorization:"Bearer secret"}},extractor:r=>r}))";
            Check(!Fetch(p,config,value,error) && error.find(L"HTTP 302") != std::wstring::npos, "redirect must not forward credentials");
            p.code = R"(({request:{url:"http://example.invalid/",method:"GET"},extractor:r=>r}))";
            Check(!Fetch(p,config,value,error), "cleartext external URL must be rejected");
            std::cout << "PASS: loopback GET/POST, HTTP errors, redirects and URL policy\n";
            return 0;
        }
        Check(BuildRequest(p,request,error), "modern JS request should compile");
        Check(request.url == "https://example.invalid/usage", "base URL placeholder mismatch");
        Check(request.headers.size()==1 && request.headers[0].second=="Bearer "+p.apiKey, "credential escaping must preserve values");
        Check(ExtractValue(p,R"({"quota":{"remaining":0}})",0,value,error) && value.remaining==0, "zero balance is valid");
        Check(ExtractValue(p,R"({"balance":-2.5,"unit":"CNY"})",0,value,error) && value.remaining==-2.5, "negative balance must be visible");
        Check(!ExtractValue(p,R"({"balance":3,"valid":false})",0,value,error), "invalid account should not show balance");
        Check(!ExtractValue(p,"{}",0,value,error), "missing balance must not become zero");
        Check(!ExtractValue(p,"not json",0,value,error), "malformed JSON must fail");
        Check(!ExtractValue(p,R"({"balance":"100"})",0,value,error), "string balance should not silently become numeric");
        p.code=R"(({request:{url:"https://example.invalid"},extractor:r=>r}))";
        Check(ExtractValue(p,R"([{"remaining":1},{"remaining":2,"planName":"second"}])",1,value,error) && value.remaining==2 && value.plan=="second", "multi-plan selection mismatch");
        Check(!ExtractValue(p,"[]",0,value,error), "empty plan array must fail");
        Check(ExtractValue(p,R"({"total":100,"used":22})",0,value,error) && value.remaining==78, "total-used balance fallback mismatch");
        Check(value.hasUsed && value.hasTotal && value.used == 22 && value.total == 100,"used/total fields must be retained");
        Check(ExtractValue(p,R"({"remaining":40,"total":100})",0,value,error) && value.hasUsed && value.used == 60,"used can be derived from total and remaining");
        Check(ExtractValue(p,R"({"remaining":40,"used":10})",0,value,error) && value.hasTotal && value.total == 50,"total can be derived from used and remaining");
        Check(ExtractValue(p,R"({"remaining":40})",0,value,error) && !value.hasTotal && !value.hasUsed,"missing metadata must not carry over");
        Check(!ExtractValue(p,R"({"remaining":1e999})",0,value,error), "non-finite balance must fail");
        p.code="(()=>{while(true){} })()";
        auto start=GetTickCount64();
        Check(!BuildRequest(p,request,error) && GetTickCount64()-start<4000, "infinite script must be interrupted");
        sqlite3* db=nullptr;
        Check(sqlite3_open(":memory:",&db)==SQLITE_OK, "test DB creation failed");
        const char* schema=R"SQL(CREATE TABLE providers(id TEXT,app_type TEXT,name TEXT,meta TEXT,settings_config TEXT,is_current INTEGER);
INSERT INTO providers VALUES('one','codex','One','{"usage_script":{"enabled":true,"code":"({request:{url:\"{{baseUrl}}/usage\",method:\"GET\"},extractor:r=>r})"}}','{"auth":{"OPENAI_API_KEY":"synthetic"},"config":"[model_providers.custom]\nbase_url = \"https://example.invalid\""}',1);
INSERT INTO providers VALUES('two','codex','Two','{}','{}',0);)SQL";
        Check(sqlite3_exec(db,schema,nullptr,nullptr,nullptr)==SQLITE_OK,"fixture creation failed");
        Config config;
        Check(LoadProvider(db,config,"{}",p,error) && p.id=="one" && p.baseUrl=="https://example.invalid" && p.apiKey=="synthetic", "current provider/credentials lookup mismatch");
        Check(!LoadProvider(db,config,R"({"currentProviderCodex":"two"})",p,error) && p.id=="two", "device-local selection must override DB flag and respect disabled usage");
        config.providerId="missing";
        Check(!LoadProvider(db,config,"{}",p,error) && p.id.empty(),"missing fixed provider must not fall back to another account");
        std::vector<ProviderChoice> choices;
        Check(ListProviders(db,choices,error) && choices.size() == 2,"provider list query");
        Check(choices[0].id == "one" && choices[0].enabled && choices[0].hasScript && !choices[1].enabled,"provider query availability metadata");
        sqlite3_close(db);
        Check(FormatValue(Value{12.5,"USD",""})==L"$12.50","currency format mismatch");
        std::cout << "PASS: modern JavaScript, escaping, numeric validation, plans, execution limits, provider selection and currency\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
