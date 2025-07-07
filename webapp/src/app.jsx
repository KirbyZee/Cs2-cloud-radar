import ReactDOM from "react-dom/client";
import React, { useEffect, useRef, useState } from "react";
import "./App.css";
import PlayerCard from "./components/PlayerCard";
import Radar from "./components/Radar";
import { getLatency, Latency } from "./components/latency";
import MaskedIcon from "./components/maskedicon";

const CONNECTION_TIMEOUT = 5000;

/* change this to '1' if you want to use offline (your own pc only) */
const USE_LOCALHOST = 0;

/* you can get your public ip from https://ipinfo.io/ip */
const PUBLIC_IP = "your ip goes here".trim();
const PORT = 22006;

const EFFECTIVE_IP = USE_LOCALHOST ? "localhost" : PUBLIC_IP.match(/[a-zA-Z]/) ? window.location.hostname : PUBLIC_IP;

const DEFAULT_SETTINGS = {
  dotSize: 1,
  bombSize: 0.5,
  showAllNames: false,
  showEnemyNames: true,
  showViewCones: false,
};

const loadSettings = () => {
  const savedSettings = localStorage.getItem("radarSettings");
  return savedSettings ? JSON.parse(savedSettings) : DEFAULT_SETTINGS;
};

const App = () => {
  const [averageLatency, setAverageLatency] = useState(0);
  const [playerArray, setPlayerArray] = useState([]);
  const [mapData, setMapData] = useState();
  const [localTeam, setLocalTeam] = useState();
  const [bombData, setBombData] = useState(); 
  const [settings, setSettings] = useState(loadSettings());

  // Save settings to local storage whenever they change
  useEffect(() => {
    localStorage.setItem("radarSettings", JSON.stringify(settings));
  }, [settings]);

  useEffect(() => {
    const fetchData = async () => {
      let webSocket = null;
      let webSocketURL = null;
      let connectionTimeout = null;

      if (PUBLIC_IP.startsWith("192.168")) {
        document.getElementsByClassName(
          "radar_message"
        )[0].textContent = `A public IP address is required! Currently detected IP (${PUBLIC_IP}) is a private/local IP`;
        return;
      }

      if (!webSocket) {
        try {
          if (USE_LOCALHOST) {
            webSocketURL = `ws://localhost:${PORT}/cs2_webradar`;
          } else {
            webSocketURL = `ws://${EFFECTIVE_IP}:${PORT}/cs2_webradar`;
          }

          if (!webSocketURL) return;
          webSocket = new WebSocket(webSocketURL);
        } catch (error) {
          document.getElementsByClassName(
            "radar_message"
          )[0].textContent = `${error}`;
        }
      }

      connectionTimeout = setTimeout(() => {
        webSocket.close();
      }, CONNECTION_TIMEOUT);

      webSocket.onopen = async () => {
        clearTimeout(connectionTimeout);
        console.info("connected to the web socket");
      };

      webSocket.onclose = async () => {
        clearTimeout(connectionTimeout);
        console.error("disconnected from the web socket");
      };

      webSocket.onerror = async (error) => {
        clearTimeout(connectionTimeout);
        document.getElementsByClassName(
          "radar_message"
        )[0].textContent = `WebSocket connection to '${webSocketURL}' failed. Please check the IP address and try again`;
        console.error(error);
      };

      webSocket.onmessage = async (event) => {
        setAverageLatency(getLatency());

        const parsedData = JSON.parse(await event.data.text());
        setPlayerArray(parsedData.m_players);
        setLocalTeam(parsedData.m_local_team);
        setBombData(parsedData.m_bomb);

        const map = parsedData.m_map;
        if (map !== "invalid") {
          setMapData({
            ...(await (await fetch(`data/${map}/data.json`)).json()),
            name: map,
          });
         
        }
      };
    };

    fetchData();
  }, []);

  // Mouse follow animation
  const mouseDotRef = useRef(null);
  useEffect(() => {
    const handleMouseMove = (e) => {
      if (mouseDotRef.current) {
        mouseDotRef.current.style.left = `${e.clientX}px`;
        mouseDotRef.current.style.top = `${e.clientY}px`;
      }
    };
    window.addEventListener("mousemove", handleMouseMove);
    return () => window.removeEventListener("mousemove", handleMouseMove);
  }, []);

  // Calculate scaling factor based on player count (max 10 cards visible at normal size)
  const maxCards = 10;
  const totalCards = playerArray.filter(p => p.m_team === 2).length + playerArray.filter(p => p.m_team === 3).length;
  const scale = totalCards > maxCards ? Math.max(0.5, maxCards / totalCards) : 1;

  // Calculate how many cards will be rendered
  const tCount = playerArray.filter(p => p.m_team === 2).length;
  const ctCount = playerArray.filter(p => p.m_team === 3).length;
  const headerHeight = 56; // px, for each header ("Terrorist Side", "CT Side")
  const gap = 8; // px, gap between cards
  const cardBaseHeight = 120; // px, estimated height of a small PlayerCard

  // Total height needed for all cards and headers
  const totalNeededHeight =
    headerHeight * 2 +
    gap * (tCount + ctCount - 2) +
    cardBaseHeight * (tCount + ctCount);

  // Get available height (subtract some margin for safety)
  const availableHeight = window.innerHeight * 0.95;

  // Calculate scale so everything fits
  const fitScale = totalNeededHeight > availableHeight
    ? Math.max(1, availableHeight / totalNeededHeight) // Don't shrink below 85%
    : 1;

  return (
    <div
      className="w-screen h-screen flex flex-row justify-between items-center backdrop-blur-[7.5px] overflow-hidden"
      style={{
        background: `none`,
        backdropFilter: `blur(7.5px)`,
      }}
    >
      <div className="animated-bg"></div>
      <div className="bubbles">
        <div className="bubble"></div>
        <div className="bubble"></div>
        <div className="bubble"></div>
        <div className="bubble"></div>
        <div className="bubble"></div>
      </div>
      <div className="mouse-follow">
        <div ref={mouseDotRef} className="mouse-dot"></div>
      </div>

      {/* Radar on the left */}
      <div className="flex flex-col justify-center items-center flex-1">
        {(playerArray.length > 0 && mapData && (
          <Radar
            playerArray={playerArray}
            radarImage={`./data/${mapData.name}/radar.png`}
            mapData={mapData}
            localTeam={localTeam}
            averageLatency={averageLatency}
            bombData={bombData}
            settings={settings}
          />
        )) || (
          <div id="radar" className="relative overflow-hidden origin-center">
            <h1 className="radar_message">
              Connected! Waiting for data from usermode
            </h1>
          </div>
        )}
      </div>

      {/* Right side: settings/ping at top, player cards below */}
      <div className="flex flex-col items-end w-[32rem] pr-8 h-full">
        {/* Settings and Ping in top right */}
        <div className="fixed top-4 right-8 z-50 flex flex-row items-center gap-4">
          <Latency
            value={averageLatency}
            settings={settings}
            setSettings={setSettings}
          />
        </div>
        {/* Player cards below settings/ping */}
        <div
          className="flex flex-row w-full h-full gap-12 mt-28"
          style={{
            transform: `scale(${fitScale})`,
            transformOrigin: "top right",
            maxHeight: "95vh",
            minHeight: 0,
          }}
        >
          {/* CT Side on the left */}
          <div className="flex flex-col w-1/2 items-end">
            <div className="w-full text-right font-bold text-lg tracking-wide uppercase opacity-80 mb-2">
              CT Side
            </div>
            <ul className="flex flex-col gap-2 w-full items-end">
              {playerArray
                .filter((player) => player.m_team == 3)
                .map((player) => (
                  <PlayerCard
                    isOnRightSide={true}
                    key={player.m_idx}
                    playerData={player}
                    settings={settings}
                    small
                  />
                ))}
            </ul>
          </div>
          {/* T Side on the right */}
          <div className="flex flex-col w-1/2 items-end">
            <div className="w-full text-right font-bold text-lg tracking-wide uppercase opacity-80 mb-2">
              Terrorist Side
            </div>
            <ul className="flex flex-col gap-2 w-full items-end">
              {playerArray
                .filter((player) => player.m_team == 2)
                .map((player) => (
                  <PlayerCard
                    isOnRightSide={true}
                    key={player.m_idx}
                    playerData={player}
                    settings={settings}
                    small
                  />
                ))}
            </ul>
          </div>
        </div>
      </div>

      {/* Bomb timer stays above all */}
      {bombData && bombData.m_blow_time > 0 && !bombData.m_is_defused && (
        <div className="absolute left-1/2 top-2 flex-col items-center gap-1 z-50">
          <div className="flex justify-center items-center gap-1">
            <MaskedIcon
              path={`./assets/icons/c4_sml.png`}
              height={32}
              color={
                (bombData.m_is_defusing &&
                  bombData.m_blow_time - bombData.m_defuse_time > 0 &&
                  `bg-radar-green`) ||
                (bombData.m_blow_time - bombData.m_defuse_time < 0 &&
                  `bg-radar-red`) ||
                `bg-radar-secondary`
              }
            />
            <span>{`${bombData.m_blow_time.toFixed(1)}s ${(bombData.m_is_defusing &&
                `(${bombData.m_defuse_time.toFixed(1)}s)`) ||
              ""
              }`}</span>
          </div>
        </div>
      )}
    </div>
  );
};

export default App;