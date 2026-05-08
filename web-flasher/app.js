// Load manifest and show the current firmware version in the banner
fetch('manifest.json')
    .then(r => r.json())
    .then(manifest => {
        const banner = document.getElementById('version-banner');
        const version = manifest.version;

        if (!version || version === '0.0.0' || manifest.builds.length === 0) {
            banner.className = 'version-banner no-release';
            banner.innerHTML =
                '&#x26A0;&#xFE0F; No firmware release published yet. ' +
                '<a href="https://github.com/joachimth/track-robot-v2/releases" target="_blank">' +
                'Check GitHub Releases</a>.';
        } else {
            banner.className = 'version-banner ready';
            banner.textContent = 'Firmware ready: v' + version;
        }
    })
    .catch(() => {
        const banner = document.getElementById('version-banner');
        banner.className = 'version-banner error';
        banner.textContent = 'Could not load firmware manifest.';
    });
