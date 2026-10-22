<template>
    <div class="card h-100">
        <div class="card-body">
            <h2>{{ $t('client_card.clients') }}</h2>
            <p class="text-body-secondary">{{ $t('client_card.clients_desc') }}</p>
            <div class="d-flex flex-wrap gap-2 align-items-center">
                <a v-for="{ platform, icon, name, link } of clients" class="btn" :class="[link && 'btn-success' || 'btn-secondary']" :href="link" target="_blank" @click="shouldOpen($event, link)"><i v-if="icon" :class="icon"></i> <span class="platform-text">{{ platform }}</span> [ {{ name }} ] </a>
            </div>
            <i class="d-block mt-3 small text-body-secondary">* {{ $t('client_card.generic_moonlight_clients_desc') }}</i>
        </div>
    </div>
</template>

<style scoped>
    .platform-text {
        font-weight: bold;
    }
</style>

<script setup>
    const clients = [
        {
            platform: 'Android',
            icon: 'fa-brands fa-android',
            name: 'Artemis',
            link: 'https://github.com/ClassicOldSong/moonlight-android'
        },
        {
            platform: 'iOS',
            icon: 'fa-brands fa-apple',
            name: 'Coming soon...',
            link: ''
        },
        {
            platform: 'Desktop',
            icon: 'fa-solid fa-desktop',
            name: 'Coming soon...',
            link: ''
        }
    ]

    const shouldOpen = (e, link) => {
        if (!link) {
            e.preventDefault();
        }
    }
</script>
