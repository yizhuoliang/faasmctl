"""
Simple example showing how to use the Faasm CPP client from Python
"""

import os
import time
from faasmctl.cpp_client import create_client
from faasmctl.util.invoke import invoke_wasm


def main():
    # Get the Faasm client (uses FAASM_INI_FILE env variable)
    ini_file = os.environ.get("FAASM_INI_FILE")
    if not ini_file:
        print("FAASM_INI_FILE environment variable not set")
        print("Please set it to the path of your Faasm INI file")
        return
    
    print(f"Using Faasm INI file: {ini_file}")
    
    # Create client
    client = create_client(ini_file)
    
    # Define a simple function invocation
    user = "polybench"
    function = "poly_deriche"
    input_data = ""

    # pure python ver
    msg = {
        "user": user,
        "function":function,
    }
    #invoke_wasm(msg, dict_out=True, host_list=None, req_dict=None,num_retries=1e5)
    
    print(f"Invoking {user}/{function} with input: {input_data}")
    
    # Invoke the function
    result = client.invoke_function(
        user=user,
        function=function,
        input_data=input_data,
        async_execution=False
    )
    
    # Print the result
    print(f"Function completed with app ID: {result['appId']}")
    print(f"Finished: {result['finished']}")
    
    if 'messageResults' in result and result['messageResults']:
        for i, msg_result in enumerate(result['messageResults']):
            print(f"Result {i}: {msg_result['outputData']}")
    else:
        print("No message results returned")

    # Get cluster status
    print("\nCluster Status:")
    in_flight_apps = client.get_inflight_apps()
    print(f"In-flight applications: {len(in_flight_apps)}")
    
    # Example of calculating cluster utilization
    num_vms = 2
    num_cpus_per_vm = 4
    idle_vms, idle_cpus, utilization = client.get_cluster_utilization(
        num_vms, num_cpus_per_vm
    )
    
    print(f"Cluster utilization: {utilization:.1f}%")
    print(f"Idle VMs: {idle_vms}/{num_vms}")
    print(f"Idle CPUs: {idle_cpus}/{num_vms * num_cpus_per_vm}")

if __name__ == "__main__":
    main()