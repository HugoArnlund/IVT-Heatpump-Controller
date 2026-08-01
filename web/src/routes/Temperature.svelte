
<script>
    import { quintInOut, quintOut, quintIn } from "svelte/easing";
    import { fade, slide } from "svelte/transition";

    export let updateSetting;
    export let mode;

    export let temp;
    let numberFirst, numberSecond;
    $: if(temp !== null) numberFirst = temp.toString().charAt(0)
    $: if(temp !== null) numberSecond = temp.toString().charAt(1)

    export let disabled;

    let changeTemp = (value) => {
        updateSetting("temp", temp + value)
    }
</script>

<div class="w-full flex flex-row justify-center items-center drop-shadow-xl {disabled ? 'opacity-20 pointer-events-none' : ''}">

    <button on:click={() => {changeTemp(-1)}} class="h-18 mt-6 mr-5 w-1/2 flex justify-end active:-translate-x-2 transition-transform duration-200">
        <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1" stroke="currentColor" class="w-14 h-full mr-14">
            <path stroke-linecap="round" stroke-linejoin="round" d="M15.75 19.5L8.25 12l7.5-7.5" />
        </svg>          
    </button>

    {#if !isNaN(temp) && !(temp === null)}
        {#if [0,3].includes(mode)}<span class="absolute font-light text-sm -bottom-8">Relativ temperatur</span>{/if}
        {#key numberFirst}
            <span 
            transition:slide={{ delay: 0, duration: 300, easing: quintOut, axis: 'y' }}
            class="text-8xl  text-center absolute pointer-events-none {numberSecond == ""?"":"mr-[2.5rem]"}">
                {numberFirst}
            </span>
        {/key}
        {#key numberSecond}
            <span 
            transition:slide={{ delay: 0, duration: 300, easing: quintOut, axis: 'y' }}
            class="text-8xl text-center absolute pointer-events-none ml-[2.5rem]">
                {numberSecond}
            </span>
        {/key}
        <span class="text-7xl absolute ml-[7.7rem] -top-2 pointer-events-none">°</span>
    {:else}
        <span 
        class="text-8xl text-center absolute pointer-events-none">
            -
        </span>
    {/if}
    
    <button on:click={() => {changeTemp(1)}} class="h-18 mt-6 ml-5 w-1/2 active:translate-x-2 transition-transform duration-200">
        <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1" stroke="currentColor" class="w-14 h-full ml-14">
            <path stroke-linecap="round" stroke-linejoin="round" d="M8.25 4.5l7.5 7.5-7.5 7.5" />
        </svg>          
    </button>
</div>