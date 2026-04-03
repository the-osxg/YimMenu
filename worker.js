// Helper: Generate a permanent token
function generateToken() { return 'OSXG-' + Math.random().toString(36).substring(2, 12).toUpperCase(); }

// Helper: Verify Discord Button Clicks (Ed25519 Signature)
function hexToUint8Array(hex) {
  return new Uint8Array(hex.match(/.{1,2}/g).map(byte => parseInt(byte, 16)));
}
async function verifyDiscordInteraction(request, publicKeyHex) {
  const signature = request.headers.get('x-signature-ed25519');
  const timestamp = request.headers.get('x-signature-timestamp');
  if (!signature || !timestamp) return false;
  const body = await request.clone().text();
  try {
    const key = await crypto.subtle.importKey("raw", hexToUint8Array(publicKeyHex), { name: "NODE-ED25519", namedCurve: "NODE-ED25519" }, false, ["verify"]);
    return await crypto.subtle.verify("NODE-ED25519", key, hexToUint8Array(signature), new TextEncoder().encode(timestamp + body));
  } catch (e) { return false; }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname;
    const method = request.method;

    const CLIENT_ID = env.CLIENT_ID;
    const CLIENT_SECRET = env.CLIENT_SECRET;
    const REDIRECT_URI = env.REDIRECT_URI;
    const GUILD_ID = env.GUILD_ID;
    const DISCORD_PUBLIC_KEY = env.DISCORD_PUBLIC_KEY;
    const BOT_TOKEN = env.BOT_TOKEN;
    const CHANNEL_ID = env.CHANNEL_ID;
    const WEBHOOK_URL = env.WEBHOOK_URL;

    // ================================================================
    // 1. DISCORD BUTTON INTERACTIONS (The Mailbox Inbox)
    // ================================================================
    if (method === "POST" && path === "/interactions") {
      const isValid = await verifyDiscordInteraction(request, DISCORD_PUBLIC_KEY);
      if (!isValid) return new Response("Bad request signature", { status: 401 });

      const interaction = await request.json();

      // Discord sends a Ping (Type 1) when you first save the URL in the portal
      if (interaction.type === 1) {
        return new Response(JSON.stringify({ type: 1 }), { headers: { "Content-Type": "application/json" } });
      }

      // Someone clicked a button! (Type 3)
      if (interaction.type === 3) {
        const customId = interaction.data.custom_id;
        const clickerId = interaction.member ? interaction.member.user.id : interaction.user.id;

        if (customId.startsWith("joinreq_")) {
          const targetRid = customId.split("_")[1];

          // Look up the joiner's registered Rockstar ID
          const joinerRid = await env.OSXG_RIDS.get(clickerId);
          if (!joinerRid) {
            const workerUrl = new URL(request.url).origin;
            return new Response(JSON.stringify({
              type: 4,
              data: { content: `You are not registered. Open this URL in your browser to register your Rockstar ID, replacing YOUR_RID with your actual RID:\n\`${workerUrl}/register_rid?token=YOUR_TOKEN&rid=YOUR_RID\`\nYour token is in the file OSXG_Token.txt inside your Yim folder.`, flags: 64 }
            }), { headers: { "Content-Type": "application/json" } });
          }

          // Queue the joiner's RID in the host's pending invites list
          const existing = await env.OSXG_PENDING.get(targetRid);
          const queue = existing ? JSON.parse(existing) : [];
          if (!queue.includes(joinerRid)) queue.push(joinerRid);
          await env.OSXG_PENDING.put(targetRid, JSON.stringify(queue), { expirationTtl: 120 });

          // Also tell the joiner's client that an invite is coming so it can auto-accept
          await env.OSXG_INVITES.put(clickerId, JSON.stringify({ status: "awaiting_invite", hostRid: targetRid }), { expirationTtl: 120 });

          return new Response(JSON.stringify({
            type: 4,
            data: { content: "✅ Join request sent! The host's game will send you an invite shortly. OSXG+ will auto-accept it.", flags: 64 }
          }), { headers: { "Content-Type": "application/json" } });
        }
      }
      return new Response("Unknown interaction", { status: 400 });
    }

    // ================================================================
    // 2. OAUTH2 LOGIN FLOW (Unchanged)
    // ================================================================
    if (method === "GET" && path === "/login") {
      const auth_id = url.searchParams.get("auth_id");
      return Response.redirect(`https://discord.com/api/oauth2/authorize?client_id=${CLIENT_ID}&redirect_uri=${encodeURIComponent(REDIRECT_URI)}&response_type=code&scope=identify%20guilds&state=${auth_id}`, 302);
    }

    if (method === "GET" && path === "/callback") {
      const code = url.searchParams.get("code");
      const auth_id = url.searchParams.get("state"); 
      if (!code) return new Response("Login failed.", { status: 400 });

      const tokenRes = await fetch("https://discord.com/api/oauth2/token", {
        method: "POST", headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: new URLSearchParams({ client_id: CLIENT_ID, client_secret: CLIENT_SECRET, grant_type: "authorization_code", code, redirect_uri: REDIRECT_URI })
      });
      const discordAccessToken = (await tokenRes.json()).access_token;

      const userData = await (await fetch("https://discord.com/api/users/@me", { headers: { "Authorization": `Bearer ${discordAccessToken}` } })).json();
      const guilds = await (await fetch("https://discord.com/api/users/@me/guilds", { headers: { "Authorization": `Bearer ${discordAccessToken}` } })).json();
      
      if (!guilds.some(g => g.id === GUILD_ID)) return new Response("Access Denied.", { status: 403 });

      const finalToken = generateToken();
      await env.OSXG_TOKENS.put(`AUTH_${auth_id}`, finalToken, { expirationTtl: 300 }); 
      await env.OSXG_TOKENS.put(finalToken, userData.id); 

      const html = `<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>OSXG+ Authorized</title><style>body{margin:0;padding:0;background-color:#050505;background-image:radial-gradient(circle at 50% 0%,#1a1a2e 0%,#050505 80%);color:#e0e0e0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;display:flex;justify-content:center;align-items:center;height:100vh}.container{background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.08);border-radius:20px;padding:50px 70px;text-align:center;box-shadow:0 10px 40px rgba(0,0,0,0.6);backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px)}.logo{max-width:280px;margin-bottom:25px;filter:drop-shadow(0 0 10px rgba(255,255,255,0.2))}h1{margin:0 0 10px 0;font-size:2.2rem;background:linear-gradient(90deg,#ffffff,#a1a1aa);-webkit-background-clip:text;-webkit-text-fill-color:transparent;letter-spacing:1px}p{margin:0;color:#9ca3af;font-size:1.1rem}.success-icon{font-size:3rem;margin-bottom:15px}</style></head><body><div class="container"><img src="https://cdn.discordapp.com/attachments/1474754859726016534/1484002180003266732/sized_rules_osxg_banner_big_text.png?ex=69c6877b&is=69c535fb&hm=f68655c840e4d0beec819a75b101ea1fd9fee5c172e9d9d854a2a93956dd6f9f" alt="OSXG+" class="logo" onerror="this.style.display='none'"><div class="success-icon">✅</div><h1>Access Granted</h1><p>You can safely close this tab and return to GTA V.</p></div></body></html>`;
      return new Response(html, { headers: { "Content-Type": "text/html" } });
    }

    if (method === "GET" && path === "/auth/check") {
      const token = await env.OSXG_TOKENS.get(`AUTH_${url.searchParams.get("auth_id")}`);
      if (!token) return new Response("Waiting...", { status: 202 });
      return new Response(JSON.stringify({ token }), { headers: { "Content-Type": "application/json" } });
    }

    // GET /register_rid — browser-accessible manual registration
    // User opens: /register_rid?token=THEIR_TOKEN&rid=THEIR_RID
    if (method === "GET" && path === "/register_rid") {
      const t = url.searchParams.get("token");
      const rid = url.searchParams.get("rid");
      if (!t || !rid) return new Response("Missing token or rid", { status: 400 });
      const discordId = await env.OSXG_TOKENS.get(t);
      if (!discordId) return new Response("Invalid token", { status: 401 });
      await env.OSXG_RIDS.put(discordId, rid, { expirationTtl: 604800 });
      return new Response(`Registered. Your Discord account is now linked to RID ${rid}.`, { status: 200 });
    }

    // ================================================================
    // 3. API ROUTES (Requires Token)
    // ================================================================
    const userToken = url.searchParams.get("token");
    if (path === "/sessions" || path === "/host" || path === "/invites/check" || path === "/register" || path === "/pending_invites" || path === "/unhost" || path === "/request_invite") {
      if (!userToken) return new Response("Missing Token", { status: 400 });
      const discordId = await env.OSXG_TOKENS.get(userToken);
      if (!discordId) return new Response("Invalid Token", { status: 401 });

      // GET /sessions
      if (method === "GET" && path === "/sessions") {
        const sessions = await env.OSXG_SESSIONS.list();
        let activeSessions = [];
        for (const key of sessions.keys) {
          const sessionData = await env.OSXG_SESSIONS.get(key.name);
          if (sessionData) activeSessions.push(JSON.parse(sessionData));
        }
        return new Response(JSON.stringify(activeSessions), { headers: { "Content-Type": "application/json" } });
      }

      // POST /host (Added the Discord Button!)
      if (method === "POST" && path === "/host") {
        const { hostName, rid, sessionType, sessionInfo } = await request.json();
        
        // Also register the host's RID so they appear in OSXG_RIDS for button clicks
        await env.OSXG_RIDS.put(discordId, rid.toString(), { expirationTtl: 604800 });
        
        const existingSession = await env.OSXG_SESSIONS.get(rid.toString());
        
        // Expiration is 120 seconds. Client must ping /host periodically to keep it alive.
        await env.OSXG_SESSIONS.put(rid.toString(), JSON.stringify({ hostName, rid, sessionType, sessionInfo, timestamp: Date.now() }), { expirationTtl: 120 });

        // Only send discord message if it's a completely newly hosted session
        if (!existingSession) {
          if (BOT_TOKEN && CHANNEL_ID) {
        const body = JSON.stringify({
              content: `<@${discordId}>`,
              embeds: [{
                title: "New GTA V Session",
                description: `**Host:** <@${discordId}> (${hostName})\n**Type:** ${sessionType}\n**RID:** \`${rid}\``,
                color: 0x5865F2,
                timestamp: new Date().toISOString()
              }],
              components: [{
                type: 1,
                components: [
                  {
                    type: 2,
                    style: 1,
                    label: "Request Invite",
                    custom_id: `joinreq_${rid.toString()}`
                  }
                ]
              }]
            });
            await fetch(`https://discord.com/api/v10/channels/${CHANNEL_ID}/messages`, {
              method: "POST", 
              headers: { 
                  "Content-Type": "application/json",
                  "Authorization": `Bot ${BOT_TOKEN}`
              },
              body: body
            });
          } else if (WEBHOOK_URL) {
            // Fallback for standard webhooks (Discord API strips buttons from standard webhooks)
            await fetch(WEBHOOK_URL, {
              method: "POST", headers: { "Content-Type": "application/json" },
              body: JSON.stringify({
                content: `**🟢 New OSXG+ Session Hosted by ${hostName}!**\nType: ${sessionType} | RID: \`${rid}\``
              })
            });
          }
        }
        return new Response("Session Hosted!", { status: 200 });
      }

      // POST /unhost (Proactively remove session from active list)
      if (method === "POST" && path === "/unhost") {
        const { rid } = await request.json();
        if (rid) {
            await env.OSXG_SESSIONS.delete(rid.toString());
            await env.OSXG_PENDING.delete(rid.toString());
        }
        return new Response("Session Unhosted", { status: 200 });
      }

      // POST /register — store Discord ID → Rockstar ID mapping
      if (method === "POST" && path === "/register") {
        const { rid } = await request.json();
        if (rid) {
          await env.OSXG_RIDS.put(discordId, rid.toString(), { expirationTtl: 604800 }); // 7 days
        }
        return new Response("Registered", { status: 200 });
      }

      // GET /pending_invites — host polls for pending join requests
      if (method === "GET" && path === "/pending_invites") {
        const hostSession = await env.OSXG_SESSIONS.get(url.searchParams.get("rid") || "");
        if (!hostSession) return new Response(JSON.stringify({ rids: [] }), { headers: { "Content-Type": "application/json" } });
        const parsed = JSON.parse(hostSession);
        const pending = await env.OSXG_PENDING.get(parsed.rid.toString());
        if (pending) {
          await env.OSXG_PENDING.delete(parsed.rid.toString());
          return new Response(JSON.stringify({ rids: JSON.parse(pending) }), { headers: { "Content-Type": "application/json" } });
        }
        return new Response(JSON.stringify({ rids: [] }), { headers: { "Content-Type": "application/json" } });
      }

      // GET /request_invite — joiner requests the host to send them an in-game invite
      if (method === "GET" && path === "/request_invite") {
        const hostRid = url.searchParams.get("host_rid");
        if (!hostRid) return new Response("Missing host_rid", { status: 400 });

        const joinerRid = await env.OSXG_RIDS.get(discordId);
        if (!joinerRid) return new Response("Not registered", { status: 404 });

        const existing = await env.OSXG_PENDING.get(hostRid);
        const queue = existing ? JSON.parse(existing) : [];
        if (!queue.includes(joinerRid)) queue.push(joinerRid);
        await env.OSXG_PENDING.put(hostRid, JSON.stringify(queue), { expirationTtl: 120 });

        return new Response("Invite requested", { status: 200 });
      }

      // GET /invites/check — joiner-side inbox (polls for Discord click recognition)
      if (method === "GET" && path === "/invites/check") {
        const invite = await env.OSXG_INVITES.get(discordId);
        if (invite) {
          await env.OSXG_INVITES.delete(discordId); // Only return once
          return new Response(invite, { headers: { "Content-Type": "application/json" } });
        }
        return new Response(JSON.stringify({ status: "empty" }), { headers: { "Content-Type": "application/json" } });
      }
    }

    return new Response("Not Found", { status: 404 });
  }
};