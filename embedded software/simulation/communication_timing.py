# Program to determine the optimal intervals for communication
# Neo Vorsatz
# Last updated: 12 April 2026

#renaming variable types
device_specs = int #communication interval and communication duration

#constants
NUM_DEVICES = 5; #number of devices trying to communicate with each other
COMMUNICATION_TIME = 5; #amount of time used to complete one communication, in seconds
MAX_INTERVAL = 20; #amount of resolution/expression of different possible intervals
SIMULATION_DURATION = 100; #total amount of time the simulation goes for, in seconds
MAX_GAP = 10; #maximum gap between intervals

#function to determine if a device is trying to communicate
def communication(time:int, device:device_specs) -> bool:
    """
    Determines whether a device is trying to communicate or not at a particular time.
    Args:
        time (int): The point in time of interest
        device (int): The specifications of how the devices tries to communicate
    Returns:
        _ (bool): Whether or not the device is trying to communicate
    Raises:
    Examples:
        >>> communication(34, (20,5))
        false
        >>> communication(12, (5,3))
        true
    """
    return (time%device)<COMMUNICATION_TIME

#function to transform any overlap
def overlap_to_interference(overlaps:int) -> float:
    if overlaps>=0:
        return overlaps
    return 0

#function to get the total interference amongst devices
def get_interference(devices:list[device_specs]) -> float:
    """
    Determines the total 'interference' of several devices trying to communicate.
    Args:
        devices (list[int]): The specifications of several devices
    Returns:
        _ (float): The total interference
    Raises:
    """
    interference = 0
    for time in range(SIMULATION_DURATION): #for each time
        #get the number of overlaps
        overlaps = -1; #0 overlap happens when 1 device is trying to communicate
        for device in devices: #for each device
            if communication(time, device):
                overlaps += 1
        #convert the overlap to interference
        interference += overlap_to_interference(overlaps)
    #return
    return interference

#function to generate a list of devices with different communication intervals
def generate_devices(iteration:int) -> list[device_specs]:
    """
    Generates a list of devices with different communication intervals, and the selection of the intervals depend on the iteration number.
    Iteration=0 will add no gaps between device intervals, iteration=1 will add a gap of 1 between the two longest intervals etc.
    Args:
        iteration (int): The iteration number
    Returns:
        _ ([int]): The specifications of several devices
    Raises:
    """
    #generate gaps, which are technically a base-`max_gap` representation of `iteration`
    gaps = [0]*NUM_DEVICES
    for i in range(NUM_DEVICES):
        gaps[i] = iteration%MAX_GAP #get the remainder
        iteration = iteration//MAX_GAP #remove the remainder
    #generate intervals
    interval = MAX_INTERVAL
    devices:list[device_specs] = []
    for i in range(NUM_DEVICES):
        #reduce the interval
        interval -= gaps[i]
        if interval<1:
            interval = 1
            #add the device
        devices.append(interval)
    #return
    return devices

if __name__=="__main__":
    #calculate the final iteration
    final_iteration = MAX_GAP**NUM_DEVICES-1
    #use the first iteration as an initial benchmark
    min_interference:float = get_interference(generate_devices(0))
    min_iteration:int = 0
    #get interation with the least interference
    for iteration in range(1, final_iteration):
        devices = generate_devices(iteration)
        interference = get_interference(devices)
        if interference<min_interference:
            min_interference = interference
            min_iteration = iteration
    #get solution
    solution = generate_devices(min_iteration)
    # for i in range(NUM_DEVICES):
    #     com_view = ""
    #     for j in range(SIMULATION_DURATION):
    #         if communication(j, solution[i]):
    #             com_view += "X"
    #         else:
    #             com_view += "_"
    #     print(com_view)
    #print the results
    print(f"Configuration: {solution}")
    print(f"Interference: {min_interference}")