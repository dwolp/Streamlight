<template>
    <!-- Legal -->
    <div class="card">
        <div class="card-body">
            <h2>{{ $t('resource_card.legal') }}</h2>
            <p class="text-body-secondary">{{ $t('resource_card.legal_desc') }}</p>
            <div class="d-flex flex-wrap gap-2">
                <a class="btn btn-secondary" href="https://github.com/ClassicOldSong/Apollo/blob/master/LICENSE"
                    target="_blank">
                    <i class="fas fa-fw fa-file-alt"></i> {{ $t('resource_card.license') }}</a>
                <a class="btn btn-secondary" href="https://github.com/ClassicOldSong/Apollo/blob/master/NOTICE"
                    target="_blank">
                    <i class="fas fa-fw fa-exclamation"></i> {{ $t('resource_card.third_party_notice') }}</a>
            </div>
        </div>
    </div>
</template>
