#include "FaasmClient.h"
#include "FaasmClientTypes.h"

// Define GOOGLE_PROTOBUF_SKIP_VERSION_CHECK before including any protobuf headers
#define GOOGLE_PROTOBUF_SKIP_VERSION_CHECK 1

// Include the protobuf header directly to get full class definitions
#include "../faasmctl/util/gen_proto_cpp/faabric.pb.h"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <vector>
#include <string>
#include <thread>
#include <future>

namespace faasmctl {

//------------------------------------------------------------------------------
// UTILITY FUNCTIONS
//------------------------------------------------------------------------------

// Split a string by delimiter
std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

// Get current timestamp in milliseconds
int64_t getTimeMillis() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// Format time duration in a human-readable format
std::string formatDuration(int64_t milliseconds) {
    std::ostringstream oss;
    
    int64_t seconds = milliseconds / 1000;
    int64_t minutes = seconds / 60;
    seconds %= 60;
    milliseconds %= 1000;
    
    if (minutes > 0) {
        oss << minutes << "m ";
    }
    
    oss << seconds << "." << std::setfill('0') << std::setw(3) << milliseconds << "s";
    
    return oss.str();
}

//------------------------------------------------------------------------------
// EXAMPLE USAGE FUNCTIONS
//------------------------------------------------------------------------------

/**
 * Example usage function to demonstrate the FaasmClient with sync invocation
 */
void exampleSyncUsage(const std::string& iniFile, 
                      const std::string& user, 
                      const std::string& function) {
    try {
        // Get singleton instance
        FaasmClient& client = FaasmClient::getInstance();
        
        // Initialize with INI file
        client.init(iniFile);
        
        // Create message dictionary
        std::map<std::string, std::string> msgDict = {
            {"user", user},
            {"function", function},
            {"input_data", "Hello from C++"}
        };
        
        // Record start time
        int64_t startTime = getTimeMillis();
        
        // Invoke function synchronously
        std::cout << "Invoking " << user << "/" << function << " synchronously..." << std::endl;
        auto result = client.invokeWasm(msgDict);
        
        // Calculate execution time
        int64_t execTime = getTimeMillis() - startTime;
        
        // Print result
        std::cout << "Execution completed in " << formatDuration(execTime) << std::endl;
        std::cout << "Results received: " << result.messageresults_size() << std::endl;
        
        // Print output from each message result
        for (int i = 0; i < result.messageresults_size(); i++) {
            const auto& msg = result.messageresults(i);
            std::cout << "Result " << i << ": " << msg.outputdata() << std::endl;
        }
        
    } catch (const FaasmClientException& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

/**
 * Example usage function to demonstrate the FaasmClient with async invocation
 */
void exampleAsyncUsage(const std::string& iniFile, 
                       const std::string& user, 
                       const std::string& function,
                       int numInvocations = 5) {
    try {
        // Get singleton instance
        FaasmClient& client = FaasmClient::getInstance();
        
        // Initialize with INI file
        client.init(iniFile);
        
        // Record start time
        int64_t startTime = getTimeMillis();
        
        // Launch multiple async invocations
        std::vector<int32_t> appIds;
        for (int i = 0; i < numInvocations; i++) {
            // Create message dictionary with unique input
            std::map<std::string, std::string> msgDict = {
                {"user", user},
                {"function", function},
                {"input_data", "Async invocation " + std::to_string(i)}
            };
            
            // Invoke function asynchronously
            std::cout << "Launching async invocation " << i << "..." << std::endl;
            auto result = client.invokeWasm(msgDict, 1, nullptr, true);
            appIds.push_back(result.appid());
        }
        
        // Wait for all invocations to complete by polling
        bool allDone = false;
        while (!allDone) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // Get status of all in-flight applications
            auto statuses = client.getInflightApps();
            
            // Check if our app IDs are still in the list
            allDone = true;
            for (int32_t appId : appIds) {
                bool found = false;
                for (const auto& status : statuses) {
                    if (status.appid() == appId && !status.finished()) {
                        found = true;
                        allDone = false;
                        break;
                    }
                }
                
                if (!found) {
                    std::cout << "App " << appId << " has completed" << std::endl;
                }
            }
            
            if (!allDone) {
                std::cout << "Waiting for " << statuses.size() << " in-flight applications..." << std::endl;
            }
        }
        
        // Calculate total execution time
        int64_t execTime = getTimeMillis() - startTime;
        std::cout << "All invocations completed in " << formatDuration(execTime) << std::endl;
        
    } catch (const FaasmClientException& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

/**
 * Example of parallel invocation with multiple threads
 */
void exampleParallelUsage(const std::string& iniFile,
                          const std::string& user,
                          const std::string& function,
                          int numThreads = 10) {
    try {
        // Get singleton instance
        FaasmClient& client = FaasmClient::getInstance();
        
        // Initialize with INI file
        client.init(iniFile);
        
        // Record start time
        int64_t startTime = getTimeMillis();
        
        // Launch multiple threads that each invoke a function
        std::vector<std::future<void>> futures;
        
        for (int i = 0; i < numThreads; i++) {
            futures.push_back(std::async(std::launch::async, [&client, user, function, i]() {
                try {
                    // Create message dictionary with thread-specific input
                    std::map<std::string, std::string> msgDict = {
                        {"user", user},
                        {"function", function},
                        {"input_data", "Thread " + std::to_string(i)}
                    };
                    
                    // Invoke function asynchronously
                    std::cout << "Thread " << i << " invoking function..." << std::endl;
                    client.invokeWasm(msgDict, 1, nullptr, true);
                } catch (const std::exception& e) {
                    std::cerr << "Thread " << i << " error: " << e.what() << std::endl;
                }
            }));
        }
        
        // Wait for all threads to complete
        for (auto& f : futures) {
            f.wait();
        }
        
        // Calculate total execution time
        int64_t execTime = getTimeMillis() - startTime;
        std::cout << "All threads launched in " << formatDuration(execTime) << std::endl;
        
        // Now monitor cluster state until everything is done
        int pollCount = 0;
        while (true) {
            auto statuses = client.getInflightApps();
            std::cout << "In-flight applications: " << statuses.size() << std::endl;
            
            if (statuses.empty() || ++pollCount > 30) {
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        execTime = getTimeMillis() - startTime;
        std::cout << "All executions completed in " << formatDuration(execTime) << std::endl;
        
    } catch (const FaasmClientException& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

}  // namespace faasmctl