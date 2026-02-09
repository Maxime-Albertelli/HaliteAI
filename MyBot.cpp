#include "hlt/game.hpp"
#include "hlt/constants.hpp"
#include "hlt/log.hpp"

#include <random>
#include <ctime>

using namespace std;
using namespace hlt;

#ifdef _DEBUG
# define LOG(X) log::log(X);
#else
# define LOG(X)
#endif // DEBUG

int main(int argc, char* argv[]) {
    unsigned int rng_seed;
    if (argc > 1) {
        rng_seed = static_cast<unsigned int>(stoul(argv[1]));
    } else {
        rng_seed = static_cast<unsigned int>(time(nullptr));
    }
    mt19937 rng(rng_seed);

    Game game; //initialisation d'une partie (une partie comporte un joueur (nous) avec son ID, une liste d'autre joueurs, une map, le tour actuel)
    
    // At this point "game" variable is populated with initial map data.
    // This is a good place to do computationally expensive start-up pre-processing.
    // As soon as you call "ready" function below, the 2 second per turn timer will start.
    game.ready("Mon pote"); 

    for (;;) {
        game.update_frame();
        shared_ptr<Player> me = game.me; 
        unique_ptr<GameMap>& game_map = game.game_map;

        vector<Command> command_queue;

        /*Gestion des vaisseaux*/
        for (const auto& ship_iterator : me->ships) { //Pour chaque vaisseau de notre bot
            shared_ptr<Ship> ship = ship_iterator.second;
            if (game_map->at(ship)->halite < constants::MAX_HALITE / 10 || ship->is_full()) { //Si le vaisseau est plein ou qu'il a fini de remplir 1/10 de sa capacité
                Direction random_direction = ALL_CARDINALS[rng() % 4]; //Choix d'une direction aléatoire
                command_queue.push_back(ship->move(random_direction)); //Le vaisseau se dirige dans une direction aléatoire
            } else {
                command_queue.push_back(ship->stay_still()); //Reste sur place
            }
        }

        /*Gestion du vaisseau mère*/
        if (
            game.turn_number <= 200 &&
            me->halite >= constants::SHIP_COST &&
            !game_map->at(me->shipyard)->is_occupied()) //Si on est à moins de 200 tours, qu'on a assez de ressource pour faire apparaitre et que la case du vaisseau est libre => faire apparaitre
        {
            command_queue.push_back(me->shipyard->spawn());
        }

        if (!game.end_turn(command_queue)) {
            break;
        }
    }

    return 0;
}

/*
Détails de la stratégie actuelle :
    Le vaisseau mère fait apparaitre des petits vaisseau dès qu'il a assez de ressource (si case libre et avant 200 tours).
    Les petits vaisseaux se déplacent ensuite aléatoirement, récupèrent un dixième de leur capacité, puis se redéplacent,
    si ils sont plein ils se déplacent à chaque tours.
    Si par chance ils retombent sur le vaisseau mère, ils peuvent se vider et reprendre leur errance. 
*/

/* 
Inventaire des fonctions pratiques pour donner des directives au bot :
    
    spawn() : fait apparaitre un petit vaisseau sur la case du vaisseau mère
    stay_still() : petit vaisseau reste sur place pour cette frame
    move() : déplace le vaisseau dans une direction donnée (cardinale)

    Getter :
        is_full() : petit vaisseau plein
        is_occupied() : est ce que la case est occupée
        at() : récupère position de l'élément (vaisseau mère ou petit vaisseau, case...)
*/

/*
Inventaire des fonctions de gestion de partie :
    push_back() : ajouter une instruction à la liste des instructions à faire
    end_turn() : regarde si la liste d'instruction est terminée
    update_frame() : actualise l'entièreté de la partie est initialise le tour actuel
*/