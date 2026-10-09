# Literature on choosing a stream's quality

Papers behind gstream's adaptive quality (gesso's `gs_abr`), and what each contributes. Each folder holds `source.pdf` and `paper.md`, a [pymupdf4llm](https://github.com/pymupdf/pymupdf4llm) extraction; both stay local and are not committed.

| Folder | Paper | What it says that matters here |
|---|---|---|
| `festive2012` | Jiang, Sekar, Zhang, "Improving Fairness, Efficiency, and Stability in HTTP-based Adaptive Video Streaming with FESTIVE", CoNEXT 2012 | Estimates bandwidth with the harmonic mean of the last 20 downloads: the right mean for rates, and robust to high outliers. |
| `mpc2015` | Yin, Jindal, Sekar, Sinopoli, "A Control-Theoretic Approach for Dynamic Adaptive Video Streaming over HTTP", SIGCOMM 2015 | Model predictive control over throughput and buffer; predicts with the harmonic mean of the last 5 downloads, and RobustMPC divides it by (1 + the recent maximum prediction error). |
| `bba2014` | Huang et al., "A Buffer-Based Approach to Rate Adaptation: Evidence from a Large Video Streaming Service", SIGCOMM 2014 (Netflix) | Choosing by buffer alone works in steady state, but the startup phase needs a throughput estimate; most of the quality difference was in the first minutes. |
| `bola2016` | Spiteri, Urgaonkar, Sitaraman, "BOLA: Near-Optimal Bitrate Adaptation for Online Videos", INFOCOM 2016 | Buffer-based choice with a utility proof; the basis of dash.js's BOLA rule. Needs buffers longer than a live stream's. |
| `panda2014` | Li et al., "Probe and Adapt: Rate Adaptation for HTTP Video Streaming At Scale", JSAC 2014 | Measured throughput overestimates the fair share when many players share a link and download in bursts; probes additively instead. |
| `pensieve2017` | Mao, Netravali, Alizadeh, "Neural Adaptive Video Streaming with Pensieve", SIGCOMM 2017 | Reinforcement learning trained in simulation. |
| `puffer2020` | Yan et al., "Learning in situ: a randomized experiment in video streaming", NSDI 2020 | A randomized trial over 38.6 years of video: learned schemes trained in simulation did not beat buffer-based control in the wild; the winner (Fugu) predicts a chunk's transmission time from its size and TCP statistics, trained on the deployment's own data, inside MPC. Its classical baseline predicts with the harmonic mean of the last 5 downloads. |
| `cs2p2016` | Sun et al., "CS2P: Improving Video Bitrate Selection and Adaptation with Data-Driven Throughput Prediction", SIGCOMM 2016 | Sessions sharing ISP and region have similar throughput; predicting the first bitrate from them, and midstream with a hidden Markov model, beat harmonic-mean MPC. |
| `dda2015` | "DDA: Cross-Session Throughput Prediction with Applications to Video Bitrate Selection", arXiv 1505.02056 | The first bitrate matters most for start-up; predicting it from other sessions gave 4 times the initial bitrate of a fixed start (2.5 Mbit/s, as services used). |

Not fetched: Spang et al., "Sammy: Smoothing Video Traffic to Be a Friendly Internet Neighbor", SIGCOMM 2023 (Netflix), which paces downloads rather than choosing quality.

## Players

- [ExoPlayer](https://github.com/androidx/media) (Google's Android player, used by the YouTube app): `DefaultBandwidthMeter` takes the median of recent downloads, each weighted by the square root of its bytes, over a window of total weight 2,000, and starts from a per-country, per-network-type estimate. `AdaptiveTrackSelection` picks the best rendition within 70% of it and moves straight there, with no step limit or timer: a step up needs 10 s buffered, or on a live stream 75% of the time to the live edge less one segment.
- [hls.js](https://github.com/video-dev/hls.js), [Shaka Player](https://github.com/shaka-project/shaka-player) and [dash.js](https://github.com/Dash-Industry-Forum/dash.js) keep two exponentially weighted averages of throughput, fast and slow, and take the lower.
