<template>
  <nav class="navbar navbar-light navbar-expand-lg navbar-background header">
    <div class="container">
      <a class="navbar-brand" href="./" title="Streamlight">
        <img src="/images/logo-apollo-45.png" height="45" alt="Streamlight">
      </a>
      <button class="navbar-toggler" type="button" data-bs-toggle="collapse" data-bs-target="#navbarSupportedContent"
              aria-controls="navbarSupportedContent" aria-expanded="false" aria-label="Toggle navigation">
        <span class="navbar-toggler-icon"></span>
      </button>
      <div class="collapse navbar-collapse" id="navbarSupportedContent">
        <ul class="navbar-nav me-auto mb-2 mb-lg-0">
          <li class="nav-item">
            <a class="nav-link" href="./"><i class="fas fa-fw fa-home"></i> {{ $t('navbar.home') }}</a>
          </li>
          <li class="nav-item">
            <a class="nav-link" href="./pin"><i class="fas fa-fw fa-unlock"></i> {{ $t('navbar.pin') }}</a>
          </li>
          <li class="nav-item">
            <a class="nav-link" href="./apps"><i class="fas fa-fw fa-stream"></i> {{ $t('navbar.applications') }}</a>
          </li>
          <li class="nav-item">
            <a class="nav-link" href="./config"><i class="fas fa-fw fa-cog"></i> {{ $t('navbar.configuration') }}</a>
          </li>
          <li class="nav-item">
            <a class="nav-link" href="./password"><i class="fas fa-fw fa-user-shield"></i> {{ $t('navbar.password') }}</a>
          </li>
          <li class="nav-item">
            <a class="nav-link" href="./troubleshooting"><i class="fas fa-fw fa-info"></i> {{ $t('navbar.troubleshoot') }}</a>
          </li>
          <li class="nav-item">
            <ThemeToggle/>
          </li>
        </ul>
      </div>
    </div>
  </nav>
</template>

<script>
import ThemeToggle from './ThemeToggle.vue'

export default {
  components: { ThemeToggle },
  created() {
    console.log("Header mounted!")
  },
  mounted() {
    let el = document.querySelector("a[href='" + document.location.pathname + "']");
    if (el) el.classList.add("active")
  }
}
</script>

<style>
.navbar-background {
  background: linear-gradient(120deg, var(--sl-nav-from) 0%, var(--sl-nav-to) 100%);
  box-shadow: 0 6px 20px -12px rgba(76, 29, 149, 0.6);
  padding-top: 0.35rem;
  padding-bottom: 0.35rem;
  position: sticky;
  top: 0;
  z-index: 1020;
}

.header .navbar-brand {
  padding: 0;
  margin-right: 1rem;
}

.header .navbar-brand img {
  height: 32px;
  width: auto;
}

.header .nav-link {
  color: rgba(255, 255, 255, .78) !important;
  border-radius: 999px;
  padding: 0.4rem 0.8rem !important;
  font-size: 0.92rem;
  transition: background-color 0.15s ease, color 0.15s ease;
}

.header .nav-link.active,
.header .nav-link:hover {
  color: #fff !important;
  background-color: rgba(255, 255, 255, .14);
}

.header .nav-link.active {
  font-weight: 600;
  background-color: rgba(255, 255, 255, .2);
}

.header .nav-link i {
  opacity: 0.9;
}

.header .navbar-toggler {
  color: #fff !important;
  border: 1px solid rgba(255, 255, 255, 0.3) !important;
}

.header .navbar-toggler-icon {
  --bs-navbar-toggler-icon-bg: url("data:image/svg+xml,%3csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 30 30'%3e%3cpath stroke='rgba%28255, 255, 255, 0.9%29' stroke-linecap='round' stroke-miterlimit='10' stroke-width='2' d='M4 7h22M4 15h22M4 23h22'/%3e%3c/svg%3e") !important;
}

@media (max-width: 991.98px) {
  .header .navbar-collapse {
    margin-top: 0.5rem;
    padding-bottom: 0.5rem;
  }
}

.form-control::placeholder {
  opacity: 0.5;
}
</style>
