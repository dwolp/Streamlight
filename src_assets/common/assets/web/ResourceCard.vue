<template>
    <div class="row g-3">
        <div class="col-lg-7">
            <div class="card h-100">
                <div class="card-body">
                    <h2>{{ $t('resource_card.resources') }}</h2>
                    <p class="text-body-secondary">{{ $t('resource_card.resources_desc') }}</p>
                    <div class="d-flex flex-wrap gap-2">
                        <!-- <a class="btn btn-secondary" href="https://app.lizardbyte.dev" target="_blank">
                            {{ $t('resource_card.lizardbyte_website') }}</a> -->
                        <a class="btn btn-secondary" href="https://github.com/ClassicOldSong/Apollo/discussions" target="_blank">
                            <i class="fab fa-fw fa-github"></i> {{ $t('resource_card.github_discussions') }}</a>
                        <a class="btn btn-secondary" href="https://github.com/ClassicOldSong/Apollo/wiki" target="_blank">
                            <i class="fab fa-fw fa-github"></i> {{ $t('resource_card.github_wiki') }}</a>
                        <a class="btn btn-secondary" href="https://github.com/ClassicOldSong/Apollo/wiki/Stuttering-Clinic" target="_blank">
                            <i class="fab fa-fw fa-github"></i> {{ $t('resource_card.github_stuttering_clinic') }}</a>
                    </div>
                </div>
            </div>
        </div>
        <!-- Legal -->
        <div class="col-lg-5">
            <div class="card h-100">
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
        </div>
    </div>
</template>
