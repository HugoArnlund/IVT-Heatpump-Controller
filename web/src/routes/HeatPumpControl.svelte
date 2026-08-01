<script>
  import { onMount } from "svelte";
  import { updateSetting } from "$lib/controller.js";
  import { settings, settingsDefault } from "$lib/store.js";
  import Temperature from "./Temperature.svelte";
  import { writeHeatpumpData, listenForResponse, listenForDeviceStatus } from "$lib/Firebase.js"

  let currentSettings;
  let disabledSettings;
  let loading = false; // Loading state for UI
  let error = ''; // Error message to show
  let success = false; // Success state for UI
  let deviceStatus = null;
  let unsubscribeDeviceStatus;

  function formatUptime(seconds) {
    const totalSeconds = Number(seconds || 0);
    const minutes = Math.floor(totalSeconds / 60);
    const hours = Math.floor(minutes / 60);
    if (hours > 0) {
      return `${hours} h ${minutes % 60} min`;
    }
    if (minutes > 0) {
      return `${minutes} min`;
    }
    return `${totalSeconds} s`;
  }

  function formatTimestamp(value) {
    if (!value) return "–";
    const date = new Date(Number(value) * 1000);
    return date.toLocaleString();
  }

  onMount(() => {
    unsubscribeDeviceStatus = listenForDeviceStatus((status) => {
      deviceStatus = status;
    });

    return () => {
      if (unsubscribeDeviceStatus) {
        unsubscribeDeviceStatus();
      }
    };
  });

  // Reactive statement to track current settings and their disabled states
  $: {
    currentSettings = $settings;
    disabledSettings = $settings.disabledSettings;
  }

  async function sendToHeatpump() {
    let data = {
      temp: currentSettings.temp,
      fan: currentSettings.fan,
      power: currentSettings.power,
      tenDegreeMode: currentSettings.tenDegreeMode,
      id: Math.floor((Math.random() * 99999)),
    };

    try {
      // Indicate loading state
      loading = true;
      error = ''; // Clear previous errors
      success = false; // Reset success state

      console.debug("Sending data to heatpump:", data);
      writeHeatpumpData(data);

      let timeout;

      console.info("Waiting for confirmation from heatpump...");
      let callback = (data, error) => {
        if (data == data.id) {
          clearTimeout(timeout);
          console.info("Received correct response from heatpump!");
          loading = false; // Hide loading spinner
          success = true; // Show success state
        }
      };

      const stopListening = listenForResponse((d, err) => {
        if (d) {
          console.log("Received data:", d);

          if (d == data.id) {
            console.info("Received correct response from heatpump!");
            
            loading = false; // Hide loading spinner
            success = true; // Show success state

            clearTimeout(timeout);
            stopListening(); // Stop listening
          }
        } else if (err) {
          console.warn("Error or no data:", err);
          loading = false;
          error = err; // Show error message
        }
      });

      timeout = setTimeout(() => {
        console.error("Timeout: No response from heatpump.");
        loading = false;
        error = 'Timeout: No response from heatpump'; // Show error message
        stopListening();

      }, 60 * 1000);
    } catch (err) {
      console.error("Error while sending data to heatpump:", err);
      loading = false;
      error = 'Error while sending data to heatpump<br>' + err; // Show error message
    }
  }
</script>

{#if currentSettings}
<div class="mt-10">
  <Temperature bind:temp={currentSettings.temp} bind:disabled={disabledSettings.temp} bind:mode={currentSettings.mode} {updateSetting}></Temperature>
</div>



<div class="w-full mt-6">
  <div class="flex flex-col items-center w-full">
    <div class="w-9/12 flex flex-col  {disabledSettings.fan ? 'opacity-20 pointer-events-none' : ''}">
      <span class="w-full mb-2 text-center">Fläkt</span>
      <div class="mx-3">
        <input class="range range-sm" type="range" min="0" max="3" on:change={(e) => updateSetting("fan", parseInt(e.target.value))} bind:value={currentSettings.fan} step="1" />
        <div class="flex justify-between w-full px-2 text-xs">
          <span>|</span>
          <span>|</span>
          <span>|</span>
          <span>|</span>
        </div>
        <div class="flex justify-between w-full px-2 text-xs">
          <span>Auto</span>
          <span>Låg</span>
          <span>Med</span>
          <span>Hög</span>
        </div>
      </div>
    </div>

    <button on:click={() => { updateSetting("power", !currentSettings.power) }} class="btn {currentSettings.power ? 'btn-success' : 'btn-error'} {currentSettings.tenDegreeMode ?"opacity-20 pointer-events-none":""} mt-10 w-8/12 rounded">
      {currentSettings.power ? 'På' : 'Av'}
    </button>
    
    <div class="flex w-8/12 mt-6">
      <div class="grid justify-between w-full grid-flow-col grid-cols-1 gap-4 drop-shadow-sm">
        <!--<button on:click={() => { updateSetting("highPower", !currentSettings.highPower) }} class="btn btn-primary h-full rounded {disabledSettings.highPower ? 'opacity-20 pointer-events-none' : ''}">
          <span class="p-4">High <br>Power</span>
        </button>-->
        <button on:click={() => { updateSetting("tenDegreeMode", !currentSettings.tenDegreeMode) }} class="btn btn-primary h-full rounded {currentSettings.tenDegreeMode ? 'btn-success' : 'btn-error'}">
          <span class="p-4">10°Läge</span>
        </button>
      </div>
    </div>


    <!--<div class="mt-6 w-9/12 flex flex-col {disabledSettings.mode ? 'opacity-20 pointer-events-none' : ''}">
      <span class="w-full mb-2 text-center">Läge</span>
      <div class="mx-3">
        <input class="range range-sm" type="range" min="0" max="3"  on:change={(e) => updateSetting("mode", parseInt(e.target.value))} bind:value={currentSettings.mode} step="1" />
        <div class="flex justify-between w-full px-2 text-xs">
          <span>|</span>
          <span>|</span>
          <span>|</span>
          <span>|</span>
        </div>
        <div class="flex justify-between w-full px-2 text-xs">
          <span>Auto</span>
          <span>Värme</span>
          <span>Kylning</span>
          <span>Avfukt</span>
        </div>
      </div>
    </div>-->
  </div>
</div>

{#if deviceStatus}
  <div class="w-8/12 p-4 mx-auto mt-4 text-sm border rounded-lg shadow-sm bg-base-100 border-base-300">
    <div class="flex items-center justify-between">
      <p class="font-semibold">Enhetsstatus</p>
      <span class="badge {deviceStatus.online ? 'badge-success' : 'badge-error'}">{deviceStatus.online ? 'Online' : 'Offline'}</span>
    </div>
    <div class="mt-2 space-y-1 text-gray-600">
      <p>Uptime: {formatUptime(deviceStatus.uptime)}</p>
      <p>Heap: {deviceStatus.freeHeap ?? '–'} bytes</p>
      <p>Min heap: {deviceStatus.minHeap ?? '–'} bytes</p>
      <p>Fragmentering: {deviceStatus.frag ?? '–'}%</p>
      <p>Signal: {deviceStatus.rssi ?? '–'} dBm</p>
      <p>Senast sedd: {formatTimestamp(deviceStatus.lastSeen ?? deviceStatus.ts)}</p>
    </div>
  </div>
{/if}

<div class="w-8/12 mt-3">
  <button on:click={sendToHeatpump} class="w-full h-16 mt-3 mb-8 rounded btn btn-primary">
    OK! <br>Skicka till värmepumpen
  </button>
</div>

{#if loading || error}
  <div class="fixed inset-0 z-50 flex items-center justify-center bg-gray-500 bg-opacity-50">
    <div class="w-8/12 p-6 bg-white rounded-lg shadow-lg">
      <div class="flex justify-center mb-4">
        <div class="w-8 h-8 loading"></div>
      </div>
      <div class="text-center">
        {#if error}
          <p class="mt-4 text-red-500">{error}</p>
          <button on:click={() => {loading = false, error = ''}} class="w-full mt-8 rounded btn btn-primary">Ok</button>
        {:else} 
          <p>Väntar på bekräftelse från värmepumpen...</p>
        {/if}
      </div>
    </div>
  </div>
{/if}

{#if success}
  <div class="fixed inset-0 z-50 flex items-center justify-center bg-gray-500 bg-opacity-50">
    <div class="p-6 bg-white rounded-lg shadow-lg">
      <div class="text-center">
        <p class="font-bold text-green-500">Datan är skickad till värmepumpen!</p>
        <button on:click={() => {success = false}} class="w-full mt-8 rounded btn btn-primary">Ok</button>
      </div>
    </div>
  </div>
{/if}
{/if}