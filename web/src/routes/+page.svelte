<script>
  // Trigger page initialization when the component is mounted
  import { onMount } from "svelte";
  import HeatPumpControl from "./HeatPumpControl.svelte";
  import Fa from 'svelte-fa'
  import { faSignOut } from '@fortawesome/free-solid-svg-icons'
  import Graph from './Graph.svelte'
  import {login} from '$lib/Firebase.js'
  import { writable } from 'svelte/store';
  import DeviceStatusCard from './DeviceStatusCard.svelte';


  let userInfo = writable();
  let loading = true; // Manage loading state locally
  let loginError = writable(); // Manage loginError state locally

  // Function to initialize the page and check for valid token
  async function initializePage() {
    try {
      login(loginError, userInfo);
    } catch (err) {
      console.error("Error initializing page:", err);
      loginError = err;
      loading = false;
    } finally {
      loading = false;
    }
  }

  onMount(() => {
    initializePage();
  });

  function reload() {
    window.location.reload();
  }
</script>

<div class="flex justify-center w-full min-h-screen select-none bg-base-200">
  <div class="flex flex-col items-center w-screen md:w-7/12 lg:w-5/12 sm">
    {#if loading}
      <div class="mt-24 loading">Loading...</div>
    {:else if $loginError}
      <h1 class="mt-10">Login failed. Please try again.</h1>
      <button class="mt-5 rounded-sm btn btn-secondary" on:click={reload}>Try Logging In Again</button>
      <p class="mt-10 text-red-500">{$loginError}</p>
    {:else if $userInfo}
      <HeatPumpControl></HeatPumpControl>
      <div class="w-8/12 h-96">
        <Graph></Graph>
      </div>
      <div class="mt-10 mb-20 w-11/12">
        <DeviceStatusCard />
      </div>
    {/if}
  </div>
</div>
