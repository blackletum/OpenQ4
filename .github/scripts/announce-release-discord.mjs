import { readFile } from "node:fs/promises";

const webhookUrl = (process.env.DISCORD_WEBHOOK_URL || "").trim();
const repoSlug = (process.env.GITHUB_REPOSITORY || "").trim();
const releaseTag = (process.env.RELEASE_TAG || "").trim();
const githubToken = (process.env.GITHUB_TOKEN || "").trim();

const releaseEmoji = (process.env.DISCORD_RELEASE_EMOJI || "").trim();
const mentions = (process.env.DISCORD_RELEASE_MENTIONS || "").trim();
const feedbackChannel = (process.env.DISCORD_FEEDBACK_CHANNEL || "the openQ4 Discord").trim();
const avatarUrl = (process.env.DISCORD_RELEASE_AVATAR_URL ||
  "https://raw.githubusercontent.com/themuffinator/OpenQ4/main/assets/img/avatar.png").trim();

function requireValue(value, name) {
  if (!value) {
    throw new Error(`${name} is required.`);
  }
}

async function loadReleaseFromEvent() {
  const eventPath = process.env.GITHUB_EVENT_PATH;
  if (!eventPath) {
    return null;
  }

  const rawEvent = await readFile(eventPath, "utf8");
  const event = JSON.parse(rawEvent);
  if (!event.release) {
    return null;
  }

  if (releaseTag && event.release.tag_name !== releaseTag) {
    return null;
  }

  return event.release;
}

async function fetchReleaseByTag() {
  requireValue(repoSlug, "GITHUB_REPOSITORY");
  requireValue(releaseTag, "RELEASE_TAG");
  requireValue(githubToken, "GITHUB_TOKEN");

  const response = await fetch(
    `https://api.github.com/repos/${repoSlug}/releases/tags/${encodeURIComponent(releaseTag)}`,
    {
      headers: {
        Accept: "application/vnd.github+json",
        Authorization: `Bearer ${githubToken}`,
        "X-GitHub-Api-Version": "2022-11-28",
      },
    },
  );

  if (!response.ok) {
    throw new Error(`GitHub release lookup failed: ${response.status} ${await response.text()}`);
  }

  return response.json();
}

async function loadRelease() {
  return (await loadReleaseFromEvent()) || fetchReleaseByTag();
}

function cleanReleaseName(release) {
  const rawName = (release.name || release.tag_name || "release").replace(/\s+/g, " ").trim();
  if (/^openq4\b/i.test(rawName)) {
    return rawName.replace(/^openq4\b/i, "openQ4");
  }
  return `openQ4 ${rawName}`;
}

function buildIntro(release, notes) {
  const lower = notes.toLowerCase();
  const mode = release.prerelease ? "prerelease" : "release";

  if (/(renderer|lighting|shadow|bloom|flare|opengl|performance|frame|gpu)/.test(lower)) {
    return `The new openQ4 ${mode} is ready, with rendering and performance work that needs real gameplay miles across supported platforms.`;
  }

  if (/(linux|macos|windows|steam deck|sdl|controller|gamepad|package|installer)/.test(lower)) {
    return `The new openQ4 ${mode} is ready, with platform and packaging improvements for cleaner installs and broader testing.`;
  }

  if (/(single-player|multiplayer|\bsp\b|\bmp\b|gameplay|map|asset|pk4|compatibility)/.test(lower)) {
    return `The new openQ4 ${mode} is ready, focused on stock-asset compatibility and in-game behavior that should be tested on real Quake 4 content.`;
  }

  return `The new openQ4 ${mode} is ready. Grab a platform package, try it with the original Quake 4 assets, and send back anything that looks off.`;
}

function extractHighlights(notes) {
  if (!notes) {
    return "";
  }

  const lines = notes.split("\n");
  const startIndex = lines.findIndex((line) => /^#{2,3}\s+Highlights\s*$/i.test(line.trim()));
  if (startIndex < 0) {
    return "";
  }

  const startLevel = lines[startIndex].trim().match(/^(#{2,3})/)?.[1].length || 3;
  const highlights = [];
  for (const line of lines.slice(startIndex + 1)) {
    const heading = line.trim().match(/^(#{1,6})\s+\S/);
    if (heading && heading[1].length <= startLevel) {
      break;
    }
    highlights.push(line);
  }

  return highlights.join("\n").replace(/\n{3,}/g, "\n\n").trim();
}

function limitText(text, maxLength) {
  if (text.length <= maxLength) {
    return text;
  }
  return `${text.slice(0, maxLength - 3).trimEnd()}...`;
}

function findAssetBySuffix(assets, suffix) {
  return assets.find((asset) => asset.name.toLowerCase().endsWith(suffix));
}

function buildDownloadLinks(release) {
  const assets = release.assets || [];
  const desired = [
    ["windows-x64-setup.exe", "Windows x64 Installer"],
    ["windows-arm64-setup.exe", "Windows ARM64 Installer"],
    ["linux-x64.tar.xz", "Linux x64"],
    ["linux-arm64.tar.xz", "Linux ARM64"],
    ["macos-arm64-opengl.tar.gz", "macOS ARM64 OpenGL"],
    ["macos-arm64-metal.tar.gz", "macOS ARM64 Metal"],
  ];

  const links = [];
  for (const [suffix, label] of desired) {
    const asset = findAssetBySuffix(assets, suffix);
    if (asset) {
      links.push(`[${label}](${asset.browser_download_url})`);
    }
  }

  links.push(`[All downloads](${release.html_url})`);
  return limitText(links.join("\n"), 1000);
}

async function postDiscordPayload(payload) {
  requireValue(webhookUrl, "DISCORD_WEBHOOK_URL");

  const response = await fetch(webhookUrl, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });

  if (!response.ok) {
    throw new Error(`Discord webhook failed: ${response.status} ${await response.text()}`);
  }
}

async function main() {
  const release = await loadRelease();
  const displayName = cleanReleaseName(release);
  const notes = (release.body || "").replace(/\r/g, "").trim();
  const intro = buildIntro(release, notes);
  const highlights = extractHighlights(notes);
  const detailsLink = `[Full release notes and downloads](${release.html_url})`;
  const feedback = `Feedback is welcome in ${feedbackChannel}.`;
  const reservedLength = intro.length + feedback.length + detailsLink.length + 8;
  const highlightsSection = highlights
    ? limitText(`## Highlights\n\n${highlights}`, Math.max(200, 2200 - reservedLength))
    : "Release notes and downloads are available on GitHub.";

  const description = limitText([
    intro,
    feedback,
    highlightsSection,
    detailsLink,
  ].join("\n\n"), 2200);

  const headline = [
    releaseEmoji,
    `${displayName} release published.`,
    mentions,
  ].filter(Boolean).join(" ");

  const payload = {
    username: "openQ4 Releases",
    avatar_url: avatarUrl,
    allowed_mentions: { parse: ["roles"] },
    content: headline,
    embeds: [
      {
        title: displayName,
        url: release.html_url,
        description,
        color: release.prerelease ? 0xd97a1f : 0x2d8f4e,
        fields: [
          { name: "Version", value: release.tag_name || releaseTag, inline: true },
          { name: "State", value: release.prerelease ? "Prerelease" : "Stable release", inline: true },
          { name: "Downloads", value: buildDownloadLinks(release) || `[Open release](${release.html_url})` },
        ],
        footer: { text: "openQ4 - open-source Quake 4 engine and game code" },
        timestamp: release.published_at || new Date().toISOString(),
      },
    ],
  };

  await postDiscordPayload(payload);
}

main().catch((error) => {
  console.error(error.message);
  process.exit(1);
});
