#  MC Ping

Query minecraft server via [SLP (Server Listing Ping)](http://wiki.vg/Server_List_Ping) to retrieve basic information (motd, player count, max players...)

## Usage

```
mcping <host> <port>
```

Host can be IP address or hostname.

Prints json data sent by server to stdout.

### Example

```
mcping minecraft.project-nemesis.cz 25565
```

```json
{
  "description":{
    "text":"§e§lMinesis Survival§r§l §b§l1.10§f§l §2§l(Minecraft Project-Nemesis)§r§l\n§7§l   §7§l§k▶▶▶§f§l VIP ECONOMY SURVIVAL LAG-FREE §7§l§k◀◀◀"
  },
    "players":{
      "max":30,
      "online":7,
      "sample":[
      {
        "id":"200d8747-b7f5-38c3-bdd0-c2d341184bd3",
        "name":"Ixy"
      },
      {
        "id":"bd959908-774c-3dab-b008-b55272af822b",
        "name":"Dejf"
      }
      ]
    },
    "version":{
      "name":"Spigot 1.10",
      "protocol":210
    },
    "favicon":"data:image/png;base64,..."
}
```

## Installation

Download binary or build from source and run.

### Downloads

## Build from source

```
gcc mcping.c -o mcping
```

## License

MIT License

Copyright (c) 2016 theodik
