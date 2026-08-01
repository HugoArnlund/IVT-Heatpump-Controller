<script>
  import { onMount } from "svelte";
  import { updateSetting } from "$lib/controller.js";
  import { settings, settingsDefault } from "$lib/store.js";
  import Temperature from "./Temperature.svelte";
  import { writeHeatpumpData, listenForResponse } from "$lib/Firebase.js"

  let currentSettings;
  let disabledSettings;
  let loading = false; // Loading state for UI
  let error = ''; // Error message to show
  let success = false; // Success state for UI
  let statusMessage = ''; // Progress message for the current command

  onMount(() => {
    return () => {};
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
      statusMessage = 'Skickar kommandot till värmepumpen...';

      console.debug("Sending data to heatpump:", data);
      const wrote = await writeHeatpumpData(data);
      if (!wrote) {
        throw new Error("Firebase-skrivning misslyckades");
      }

      let timeout;

      console.info("Väntar på bekräftelse från värmepumpen...");
      const stopListening = listenForResponse((response, err) => {
        if (err) {
          console.warn("Error or no data:", err);
          loading = false;
          error = err;
          statusMessage = 'Kunde inte läsa bekräftelsen från värmepumpen.';
          return;
        }

        if (!response) {
          return;
        }

        if (response.id != null && response.id !== data.id) {
          return;
        }

        if (response.status === 'received') {
          statusMessage = response.message || 'Kommandot mottogs av styrenheten.';
          return;
        }

        if (response.status === 'executed') {
          console.info("Received successful execution response from heatpump!");
          loading = false;
          success = true;
          statusMessage = response.message || 'Kommandot utfördes framgångsrikt.';
          clearTimeout(timeout);
          stopListening();
          return;
        }

        if (response.status === 'failed') {
          console.warn("Heat pump reported a failed execution:", response.error || response.message);
          loading = false;
          error = response.error || response.message || 'Kommandot kunde inte utföras.';
          statusMessage = 'Kommandot avvisades eller misslyckades.';
          clearTimeout(timeout);
          stopListening();
        }
      });

      timeout = setTimeout(() => {
        console.error("Tidsgräns: inget svar från värmepumpen.");
        loading = false;
        error = 'Tidsgräns: Inget svar från värmepumpen.';
        statusMessage = 'Ingen bekräftelse mottogs inom tidsgränsen.';
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
          <button on:click={() => {loading = false, error = '', statusMessage = ''}} class="w-full mt-8 rounded btn btn-primary">Ok</button>
        {:else} 
          <p>{statusMessage || 'Väntar på bekräftelse från värmepumpen...'}</p>
        {/if}
      </div>
    </div>
  </div>
{/if}

{#if success}
  <div class="fixed inset-0 z-50 flex items-center justify-center bg-gray-500 bg-opacity-50">
    <div class="p-6 bg-white rounded-lg shadow-lg">
      <div class="text-center">
        <p class="font-bold text-green-500">{statusMessage || 'Datan är skickad till värmepumpen!'}</p>
        <button on:click={() => {success = false, statusMessage = ''}} class="w-full mt-8 rounded btn btn-primary">Ok</button>
      </div>
    </div>
  </div>
{/if}
{/if}