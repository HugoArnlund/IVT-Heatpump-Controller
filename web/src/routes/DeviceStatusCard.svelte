<script>
  import { onMount } from "svelte";
  import { listenForDeviceStatus } from "$lib/Firebase.js";

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
</script>

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
