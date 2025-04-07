#!/usr/bin/env python3
"""
Example script demonstrating how to monitor cluster utilization and running applications
using the faasmctl Python client.
"""

import os
import time
import argparse
from tabulate import tabulate
from typing import Dict, List, Any

from faasmctl.cpp_client.wrapper import create_client

def format_app_info(app_info: Dict[str, Any]) -> str:
    """Format application information into a readable string"""
    app_id = app_info.get("appId")
    functions = app_info.get("functions", [])
    hosts = app_info.get("hosts", [])
    
    # Handle multiple or no functions
    func_str = ", ".join(functions) if functions else "Unknown"
    host_str = ", ".join(hosts) if hosts else "Unknown"
    
    return f"App {app_id}: {func_str} on {host_str}"

def display_cluster_stats(
    idle_vms: int,
    idle_cpus: int,
    utilization: float,
    host_usage: Dict[str, int],
    running_apps: List[Dict[str, Any]]
):
    """Display formatted cluster statistics"""
    print("\n===== CLUSTER UTILIZATION =====")
    print(f"Idle VMs: {idle_vms}")
    print(f"Idle CPUs: {idle_cpus}")
    print(f"Utilization: {utilization:.2f}%")
    
    # Show host usage as a table
    host_table = [[host, usage] for host, usage in host_usage.items()]
    if host_table:
        print("\n----- Host Usage -----")
        print(tabulate(host_table, headers=["Host", "CPUs Used"]))
    
    # Show running applications as a table
    if running_apps:
        app_table = []
        for app in running_apps:
            app_id = app.get("appId")
            expected_msgs = app.get("expectedNumMessages", 0)
            
            # Extract hosts and functions from messageResults
            hosts = set()
            functions = set()
            
            if "messageResults" in app:
                for msg in app["messageResults"]:
                    # Get host information
                    if "executedHost" in msg and msg["executedHost"]:
                        hosts.add(msg["executedHost"])
                    
                    # Get function information
                    user = msg.get("user", "")
                    function = msg.get("function", "")
                    if user and function:
                        functions.add(f"{user}/{function}")
            
            # Format as comma-separated strings
            hosts_str = ", ".join(hosts) if hosts else "Unknown"
            functions_str = ", ".join(functions) if functions else "Unknown"
                
            app_table.append([app_id, functions_str, hosts_str, expected_msgs])
        
        print("\n----- Running Applications -----")
        print(tabulate(app_table, headers=["App ID", "Function", "Hosts", "Expected Messages"]))
    else:
        print("\nNo applications currently running.")

def monitor_cluster(
    ini_file: str,
    num_vms: int,
    num_cpus_per_vm: int,
    interval: int = 5,
    continuous: bool = False
):
    """
    Monitor the Faasm cluster utilization and running applications
    
    Args:
        ini_file: Path to the Faasm INI configuration file
        num_vms: Number of VMs in the cluster
        num_cpus_per_vm: Number of CPUs per VM
        interval: Time interval between updates in seconds (for continuous mode)
        continuous: Whether to continuously monitor or just once
    """
    # Create client
    client = create_client(ini_file)
    
    try:
        if continuous:
            print(f"Monitoring Faasm cluster every {interval} seconds. Press Ctrl+C to exit.")
            while True:
                # Clear screen for better visualization
                os.system('clear' if os.name == 'posix' else 'cls')
                
                # Get cluster stats
                stats = client.get_cluster_utilization(num_vms, num_cpus_per_vm)
                display_cluster_stats(*stats)
                
                # Show timestamp
                print(f"\nLast updated: {time.strftime('%Y-%m-%d %H:%M:%S')}")
                print("Press Ctrl+C to exit...")
                
                time.sleep(interval)
        else:
            # Just get stats once
            stats = client.get_cluster_utilization(num_vms, num_cpus_per_vm)
            display_cluster_stats(*stats)
    except KeyboardInterrupt:
        print("\nMonitoring stopped by user.")
    except Exception as e:
        print(f"Error monitoring cluster: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Monitor Faasm cluster utilization")
    parser.add_argument("--ini", type=str, default=os.environ.get("FAASM_INI_FILE", ""),
                        help="Path to Faasm INI file (default: FAASM_INI_FILE env var)")
    parser.add_argument("--vms", type=int, default=1,
                        help="Number of VMs in the cluster (default: 1)")
    parser.add_argument("--cpus", type=int, default=1,
                        help="CPUs per VM (default: 1)")
    parser.add_argument("--interval", type=int, default=5,
                        help="Update interval in seconds (default: 5s)")
    parser.add_argument("--continuous", action="store_true",
                        help="Continuously monitor the cluster")
                        
    args = parser.parse_args()
    
    if not args.ini:
        parser.error("No INI file provided. Use --ini or set FAASM_INI_FILE env var.")
    
    monitor_cluster(
        args.ini,
        args.vms,
        args.cpus,
        args.interval,
        args.continuous
    ) 