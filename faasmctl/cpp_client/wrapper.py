"""
Python wrapper for the C++ FaasmClient
Provides higher-level functions and Pythonic interface
"""

import os
import time
from typing import Dict, List, Tuple, Optional, Any, Union
from faasmctl.faasm_client_cpp import FaasmClient


class FaasmClientWrapper:
    """
    Pythonic wrapper around the C++ FaasmClient
    """
    def __init__(self, ini_file: Optional[str] = None):
        """
        Initialize the Faasm client
        
        Args:
            ini_file: Path to the Faasm INI file, defaults to FAASM_INI_FILE env var
        """
        self.client = FaasmClient.get_instance()
        
        if ini_file is None:
            ini_file = os.environ.get("FAASM_INI_FILE", "")
            if not ini_file:
                raise ValueError("No INI file provided and FAASM_INI_FILE not set")
        
        self.client.init(ini_file)
    
    def invoke_function(self, 
                        user: str, 
                        function: str, 
                        input_data: str = "", 
                        num_messages: int = 1, 
                        host_list: Optional[List[str]] = None,
                        async_execution: bool = False) -> Dict[str, Any]:
        """
        Invoke a Faasm function
        
        Args:
            user: User owning the function
            function: Function name to invoke
            input_data: Input data string
            num_messages: Number of messages to create
            host_list: Optional list of hosts to execute on
            async_execution: Whether to return immediately or wait for completion
            
        Returns:
            Dict containing the execution results
        """
        msg_dict = {
            "user": user,
            "function": function,
            "input_data": input_data
        }

        # Force host_list to be a list if it's None
        if host_list is None:
            host_list = []

        return self.client.invoke_wasm(
            msg_dict,        # dict[str, str]
            num_messages,    # int
            host_list,       # now a list[str], not None
            async_execution  # bool
        )
    
    def get_inflight_apps(self) -> List[Dict[str, Any]]:
        """
        Get information about in-flight applications
        
        Returns:
            List of dicts containing application statuses
        """
        return self.client.get_inflight_apps()
    
    def wait_for_completion(self, 
                          app_ids: List[int], 
                          poll_interval_secs: float = 1.0,
                          timeout_secs: Optional[float] = None) -> bool:
        """
        Wait for completion of specific application IDs
        
        Args:
            app_ids: List of application IDs to wait for
            poll_interval_secs: How often to poll for status
            timeout_secs: Maximum time to wait (None = wait indefinitely)
            
        Returns:
            True if all completed successfully, False if timeout occurred
        """
        start_time = time.time()
        
        while True:
            # Check timeout
            if timeout_secs is not None and (time.time() - start_time) > timeout_secs:
                return False
            
            # Get in-flight applications
            in_flight_apps = self.get_inflight_apps()
            
            # Check if our app IDs are still in-flight and not finished
            all_done = True
            for app_id in app_ids:
                for app in in_flight_apps:
                    if app["appId"] == app_id and not app["finished"]:
                        all_done = False
                        break
            
            if all_done:
                return True
            
            # Wait before polling again
            time.sleep(poll_interval_secs)
    
    def get_cluster_utilization(self, 
                              num_vms: int, 
                              num_cpus_per_vm: int) -> Tuple[int, int, float]:
        """
        Calculate cluster utilization
        
        Args:
            num_vms: Number of VMs in the cluster
            num_cpus_per_vm: Number of CPUs per VM
            
        Returns:
            Tuple of (idle_vms, idle_cpus, utilization_percentage)
        """
        in_flight_apps = self.get_inflight_apps()
        
        # Calculate idle VMs and CPUs using direct tracking
        # Ensure both values are integers
        num_vms_int = int(num_vms)
        num_cpus_per_vm_int = int(num_cpus_per_vm)
        total_cpus = num_vms_int * num_cpus_per_vm_int
        
        # Track worker occupation
        worker_occupation = {}
        total_used_cpus = 0
        
        # Count used CPUs from message results
        for app in in_flight_apps:
            if "messageResults" in app:
                for msg in app["messageResults"]:
                    if "executedHost" in msg and msg["executedHost"]:
                        ip = msg["executedHost"]
                        worker_occupation[ip] = worker_occupation.get(ip, 0) + 1
                        total_used_cpus += 1
        
        # Calculate idle resources
        idle_vms = num_vms - len(worker_occupation)
        idle_cpus = total_cpus - total_used_cpus
        
        # Calculate utilization percentage
        utilization = (total_cpus - idle_cpus) / total_cpus * 100.0 if total_cpus > 0 else 0.0
        
        return idle_vms, idle_cpus, utilization
    
    def batch_invoke(self, 
                    user: str,
                    function: str,
                    inputs: List[str],
                    parallel: bool = True,
                    max_concurrent: Optional[int] = None) -> List[Dict[str, Any]]:
        """
        Invoke multiple functions with different inputs
        
        Args:
            user: User owning the function
            function: Function name to invoke
            inputs: List of input strings, one per invocation
            parallel: Whether to execute in parallel (async) or sequential
            max_concurrent: Maximum number of concurrent executions
            
        Returns:
            List of results for each invocation
        """
        results = []
        app_ids = []
        
        # Determine batch strategy
        if parallel:
            # Process in batches if max_concurrent is specified
            batch_size = max_concurrent if max_concurrent else len(inputs)
            
            for i in range(0, len(inputs), batch_size):
                batch = inputs[i:i+batch_size]
                batch_app_ids = []
                
                # Launch batch
                for input_data in batch:
                    result = self.invoke_function(
                        user, function, input_data, async_execution=True
                    )
                    results.append(result)
                    batch_app_ids.append(result["appId"])
                
                # Wait for batch to complete
                self.wait_for_completion(batch_app_ids)
                app_ids.extend(batch_app_ids)
        else:
            # Sequential execution
            for input_data in inputs:
                result = self.invoke_function(
                    user, function, input_data, async_execution=False
                )
                results.append(result)
                app_ids.append(result["appId"])
        
        # Update results with final status
        final_results = []
        for app_id in app_ids:
            # Look for this app in the in-flight apps
            in_flight_apps = self.get_inflight_apps()
            found = False
            
            for app in in_flight_apps:
                if app["appId"] == app_id:
                    final_results.append(app)
                    found = True
                    break
            
            if not found:
                # App is no longer in-flight, so it must be complete
                # Add placeholder result
                final_results.append({"appId": app_id, "finished": True})
        
        return final_results


# Factory function to create a client instance
def create_client(ini_file: Optional[str] = None) -> FaasmClientWrapper:
    """
    Create a new FaasmClientWrapper instance
    
    Args:
        ini_file: Path to the Faasm INI file, defaults to FAASM_INI_FILE env var
        
    Returns:
        FaasmClientWrapper instance
    """
    return FaasmClientWrapper(ini_file)