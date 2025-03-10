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
            Dict containing the execution results and metrics
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
    
    def check_async_status(self, app_id: int, expected_num_messages: int) -> Dict[str, Any]:
        """
        Check the status of an asynchronous function invocation
        
        Args:
            app_id: Application ID returned from an async invoke_function call
            expected_num_messages: Expected number of messages in the response
            
        Returns:
            Dict containing the current status of the execution and metrics if completed
        """
        return self.client.check_async_status(app_id, expected_num_messages)
    
    def get_inflight_apps(self) -> List[Dict[str, Any]]:
        """
        Get information about in-flight applications
        
        Returns:
            List of dicts containing application statuses
        """
        return self.client.get_inflight_apps()
    
    def get_execution_metrics(self, app_id: int, expected_num_messages: int = 1) -> List[Dict[str, Any]]:
        """
        Get detailed execution metrics for a completed function
        
        Args:
            app_id: Application ID to get metrics for
            expected_num_messages: Expected number of messages
            
        Returns:
            List of message metrics with execution details
        """
        status = self.check_async_status(app_id, expected_num_messages)
        return status.get("metrics", [])
    
    def wait_for_completion(self, 
                           app_ids: List[int],
                           expected_num_messages: int = 1, 
                           poll_interval_secs: float = 1.0,
                           timeout_secs: Optional[float] = None) -> Dict[int, Dict[str, Any]]:
        """
        Wait for completion of specific application IDs
        
        Args:
            app_ids: List of application IDs to wait for
            expected_num_messages: Expected number of messages per app (for checking status)
            poll_interval_secs: How often to poll for status
            timeout_secs: Maximum time to wait (None = wait indefinitely)
            
        Returns:
            Dict mapping app_id to its final status dict, with metrics included
        """
        start_time = time.time()
        remaining_app_ids = set(app_ids)
        results = {}
        
        while remaining_app_ids:
            # Check timeout
            if timeout_secs is not None and (time.time() - start_time) > timeout_secs:
                # For any remaining apps, get their current status
                for app_id in remaining_app_ids:
                    if app_id not in results:
                        try:
                            results[app_id] = self.check_async_status(app_id, expected_num_messages)
                        except Exception:
                            results[app_id] = {"appId": app_id, "finished": False, "timedOut": True}
                return results
            
            # Check status of remaining app IDs
            completed_app_ids = set()
            
            for app_id in remaining_app_ids:
                try:
                    status = self.check_async_status(app_id, expected_num_messages)
                    if status["finished"]:
                        results[app_id] = status
                        completed_app_ids.add(app_id)
                except Exception:
                    # If we can't check status, assume it's not done
                    pass
            
            # Remove completed apps from the remaining set
            remaining_app_ids -= completed_app_ids
            
            if remaining_app_ids:
                # Wait before polling again
                time.sleep(poll_interval_secs)
        
        return results
    
    def get_cluster_utilization(self, 
                              num_vms: int, 
                              num_cpus_per_vm: int) -> Tuple[int, int, float, Dict[str, int]]:
        """
        Calculate cluster utilization
        
        Args:
            num_vms: Number of VMs in the cluster
            num_cpus_per_vm: Number of CPUs per VM
            
        Returns:
            Tuple of (idle_vms, idle_cpus, utilization_percentage, host_usage)
            where host_usage is a dict mapping host IPs to number of used CPUs
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
        
        return idle_vms, idle_cpus, utilization, worker_occupation
    
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
            List of results for each invocation, including execution metrics
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
                batch_results = self.wait_for_completion(batch_app_ids)
                
                # Update the results with the final status including metrics
                for i, app_id in enumerate(batch_app_ids):
                    if app_id in batch_results:
                        # Find and update the corresponding result in the results list
                        for j, res in enumerate(results):
                            if res["appId"] == app_id:
                                results[j] = batch_results[app_id]
                                break
                
                app_ids.extend(batch_app_ids)
        else:
            # Sequential execution
            for input_data in inputs:
                result = self.invoke_function(
                    user, function, input_data, async_execution=False
                )
                results.append(result)
                app_ids.append(result["appId"])
        
        return results
    
    def analyze_execution_times(self, results: List[Dict[str, Any]]) -> Dict[str, Any]:
        """
        Analyze execution metrics from a batch of function invocations
        
        Args:
            results: List of result dictionaries from batch_invoke
            
        Returns:
            Dict with execution time statistics
        """
        if not results:
            return {"error": "No results to analyze"}
        
        durations = []
        hosts = set()
        
        for result in results:
            if "metrics" in result:
                for metric in result["metrics"]:
                    if "durationMs" in metric and metric["durationMs"] is not None and metric["durationMs"] > 0:
                        durations.append(metric["durationMs"])
                    
                    if "executedHost" in metric and metric["executedHost"]:
                        hosts.add(metric["executedHost"])
        
        if not durations:
            return {"error": "No valid duration data found in results"}
        
        # Calculate statistics
        durations.sort()
        total = sum(durations)
        count = len(durations)
        
        stats = {
            "count": count,
            "min_ms": durations[0],
            "max_ms": durations[-1],
            "avg_ms": total / count,
            "median_ms": durations[count // 2] if count % 2 != 0 else (durations[count // 2 - 1] + durations[count // 2]) / 2,
            "total_ms": total,
            "unique_hosts": len(hosts),
            "hosts": list(hosts)
        }
        
        # Calculate percentiles
        if count >= 10:
            stats["p90_ms"] = durations[int(count * 0.9)]
            stats["p95_ms"] = durations[int(count * 0.95)]
            stats["p99_ms"] = durations[int(count * 0.99)]
        
        return stats


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