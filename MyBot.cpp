#include "hlt/game.hpp"
#include "hlt/constants.hpp"
#include "hlt/log.hpp"

#include <random>
#include <ctime>
#include <set>
#include <map>

#include <unordered_map>
#include <unordered_set>

using namespace std;
using namespace hlt;

#ifdef _DEBUG
# define LOG(X) log::log(X);
#else
# define LOG(X)
#endif // DEBUG

enum class ShipState {
    HARVESTING,
    RETURNING
};

// Fonction pour trouver la base (Shipyard ou Dropoff) la plus proche d'un vaisseau
Position get_closest_dropoff(shared_ptr<Ship> ship, shared_ptr<Player> me, const unique_ptr<GameMap>& game_map) {
    // On initialise avec la distance vers le vaisseau mère
    Position closest_base = me->shipyard->position;
    int min_distance = game_map->calculate_distance(ship->position, closest_base);

    // On vérifie la distance vers tous les Dropoffs existants
    for (const auto& dropoff_pair : me->dropoffs) {
        shared_ptr<Dropoff> dropoff = dropoff_pair.second;
        int dist = game_map->calculate_distance(ship->position, dropoff->position);

        // Si ce Dropoff est plus proche, il devient notre nouvelle cible
        if (dist < min_distance) {
            min_distance = dist;
            closest_base = dropoff->position;
        }
    }

    return closest_base;
}

int main(int argc, char* argv[]) {
    unsigned int rng_seed;
    if (argc > 1) rng_seed = static_cast<unsigned int>(stoul(argv[1]));
    else rng_seed = static_cast<unsigned int>(time(nullptr));
    mt19937 rng(rng_seed);

    Game game;
    game.ready("Mon pote");

    unordered_map<EntityId, ShipState> ship_states; //état des vaisseaux (RECOLTE, RETOUR VAISSEAU MERE)

    for (;;) {
        game.update_frame();
        vector<Command> command_queue;
        shared_ptr<Player> me = game.me;
        unique_ptr<GameMap>& game_map = game.game_map;

        // "Carte" des positions où nos vaisseaux vont aller ce tour-ci pour éviter les collisions
        // On remplit ça à chaque décision de mouvement
        //set<Position> intended_positions;

        // Grille 2D représentant une carte des positions où nos vaisseaux vont se rendre ce tour-ci
        // On initialise toutes les cases à "false" au début du tour
        vector<vector<bool>> intended_grid(game_map->width, vector<bool>(game_map->height, false));

        unordered_map<EntityId, Command> ship_commands; // Stocke les ordres avant validation
        unordered_set<EntityId> processed_ships;        // Retient les vaisseaux qui ont déjà une commande

        // On autorise la création d'un seul dropoff par tour si on est riche
        // On limite aussi le nombre total de dropoffs pour ne pas tapisser la carte
        int max_dropoffs = 3; // à ajuster selon la taille de la carte
        bool can_build_dropoff = (me->halite >= 5000) && (me->dropoffs.size() < max_dropoffs);

        //* --Gestion des petits vaisseaux-- *
        // * --Mise à jour des états de la flotte et règle des 10%-- *
        for (const auto& ship_iterator : me->ships) {

            const auto& ship = ship_iterator.second;
            EntityId id = ship->id;

            //Nouveaux vaisseaux ou vaisseaux vide prennent l'état HARVESTING
            if (ship_states.find(id) == ship_states.end() || ship->halite == 0) {
                ship_states[id] = ShipState::HARVESTING;
            }

            //Si vaisseau plein => état retour vaisseau mère
            else if (ship->halite >= constants::MAX_HALITE * 0.95) {
                ship_states[id] = ShipState::RETURNING;
            }

            // Retour à la base la plus proche avant fin de la partie
            Position closest_base = get_closest_dropoff(ship, me, game_map);
            int turns_to_home = game_map->calculate_distance(ship->position, closest_base);
            if (game.turn_number > constants::MAX_TURNS - (turns_to_home + 10)) {
                ship_states[id] = ShipState::RETURNING;
            }

            // Un vaisseau ne peut bouger que s'il a au moins 10% du halite de sa case actuelle
            int move_cost = game_map->at(ship->position)->halite / 10;
            if (ship->halite < move_cost) {
                ship_commands[id] = ship->stay_still();
                processed_ships.insert(id);
                // intended_positions.insert(ship->position); // vaisseau reste immobile => réserver la case
                intended_grid[ship->position.x][ship->position.y] = true; // vaisseau reste immobile => réserver la case
            }
        }

        // * --Comportement des vaisseaux qui rentrent et swapping (à gérer en priorité)-- *
        for (const auto& ship_iterator : me->ships) {
            const auto& ship = ship_iterator.second;
            EntityId id = ship->id;

            // Si le vaisseau est bloqué par la règle des 10% ou a déjà swappé => passer au suivant
            if (processed_ships.count(id)) continue;

            //Comportement de l'état retour
            if (ship_states[id] == ShipState::RETURNING) {
                Position target_base = get_closest_dropoff(ship, me, game_map);

                if (ship->position == target_base) { // On vérifie si on est sur la base cible
                    // Reste sur place pour déposer
                    //intended_positions.insert(ship->position);
                    intended_grid[ship->position.x][ship->position.y] = true;
                    ship_commands[id] = ship->stay_still();
                    processed_ships.insert(id);
                }
                else {
                    // On navigue vers la base cible
                    vector<Direction> options = game_map->get_unsafe_moves(ship->position, target_base);
                    bool moved = false;

                    for (Direction dir : options) {
                        Position target_pos = ship->position.directional_offset(dir);

                        // Si la case est déjà réservée par un autre vaisseau rentrant => on passe
                        //if (intended_positions.count(target_pos) > 0) continue;
                        if (intended_grid[target_pos.x][target_pos.y]) continue;

                            // Swapping : y a-t-il un de nos vaisseaux sur cette case ?
                            if (game_map->at(target_pos)->is_occupied() &&
                                game_map->at(target_pos)->ship->owner == me->id) {

                                shared_ptr<Ship> ally = game_map->at(target_pos)->ship;

                                // Si l'allié n'est pas bloqué par la règle des 10% et n'a pas encore d'ordre
                                if (processed_ships.count(ally->id) == 0) {
                                    ship_commands[id] = ship->move(dir);
                                    ship_commands[ally->id] = ally->move(invert_direction(dir)); // L'allié recule

                                    processed_ships.insert(id);
                                    processed_ships.insert(ally->id);

                                    //intended_positions.insert(target_pos);
                                    intended_grid[target_pos.x][target_pos.y] = true;
                                    //intended_positions.insert(ship->position); // L'allié prend notre ancienne case
                                    intended_grid[ship->position.x][ship->position.y] = true; // L'allié prend notre ancienne case
                                    moved = true;
                                    break;
                                }
                            }
                            // Case libre
                            else if (!game_map->at(target_pos)->is_occupied()) {
                                ship_commands[id] = ship->move(dir);
                                processed_ships.insert(id);
                                //intended_positions.insert(target_pos);
                                intended_grid[target_pos.x][target_pos.y] = true;
                                moved = true;
                                break;
                            }
                    }

                    // Si on est complètement coincé, on reste sur place
                    if (!moved) {
                        ship_commands[id] = ship->stay_still();
                        processed_ships.insert(id);
                        //intended_positions.insert(ship->position);
                        intended_grid[ship->position.x][ship->position.y] = true;
                    }
                }
            }
        }

        // * --Traiter ensuite les vaisseaux qui récoltent l'halite-- *
        for (const auto& ship_iterator : me->ships) {
            const auto& ship = ship_iterator.second;
            EntityId id = ship->id;

            // Si le vaisseau est bloqué par la règle des 10% ou a déjà swappé => passer au suivant
            if (processed_ships.count(id)) continue;

            //Comportement de l'état récolte 
            if (ship_states[id] == ShipState::HARVESTING) {

                // Décider si on crée un dropoff
                Position closest_base = get_closest_dropoff(ship, me, game_map);
                int dist_to_base = game_map->calculate_distance(ship->position, closest_base);

                // Si on a l'autorisation, qu'on est à plus de 15 cases d'une base, 
                // et que la case sur laquelle on se trouve est un peu riche
                if (can_build_dropoff && dist_to_base > 15 && game_map->at(ship->position)->halite > 500) {

                    ship_commands[id] = ship->make_dropoff();
                    processed_ships.insert(id);
                    //intended_positions.insert(ship->position); // Le dropoff devient un obstacle permanent !
                    intended_grid[ship->position.x][ship->position.y] = true; // Le dropoff devient un obstacle permanent

                    can_build_dropoff = false; // On retire l'autorisation pour les autres vaisseaux ce tour-ci
                    continue; // On passe au vaisseau suivant
                }

                // Si la case actuelle contient encore beaucoup de ressources (100 ici),
                // on reste dessus
                if (game_map->at(ship)->halite > 100 && !ship->is_full()) {
                    //intended_positions.insert(ship->position);
                    intended_grid[ship->position.x][ship->position.y] = true;
                    ship_commands[id] = ship->stay_still();
                    processed_ships.insert(id);
                }
                else {
                    // Recherche de la case proche la plus intéressante
                    int max_score = -1;
                    Direction best_dir = Direction::STILL;
                    Position future_pos = ship->position; // Sécurité : cible sa propre position par défaut

                    for (const auto& dir : ALL_CARDINALS) {
                        Position target_pos = ship->position.directional_offset(dir);

                        // Verification de si la case est déjà prévue par un autre vaisseau => éviter les collisions
                        //if (intended_positions.count(target_pos)) continue;
                        if (intended_grid[target_pos.x][target_pos.y]) continue;

                        // Eviter les vaisseaux ennemis
                        if (game_map->at(target_pos)->is_occupied() && game_map->at(target_pos)->ship->owner != me->id) continue;

                        int cell_halite = game_map->at(target_pos)->halite;

                        // Définition des scores d'intéret pour les cases adjacentes
                        if (cell_halite > max_score) {
                            max_score = cell_halite;
                            best_dir = dir;
                            future_pos = target_pos;
                        }
                    }

                    //intended_positions.insert(future_pos); // On réserve la case
                    intended_grid[future_pos.x][future_pos.y] = true; // On réserve la case
                    ship_commands[id] = ship->move(best_dir);
                    processed_ships.insert(id);
                }
            }
        }

        // Transférer les commandes dans la command_queue
        for (const auto& pair : ship_commands) {
            command_queue.push_back(pair.second);
        }

        //* --Gestion du vaisseau mère-- *
        // On vérifie si un vaisseau a prévu de venir sur le vaisseau mère à ce tour
        //bool is_shipyard_safe = (intended_positions.count(me->shipyard->position) == 0);
        bool is_shipyard_safe = !intended_grid[me->shipyard->position.x][me->shipyard->position.y];

        if (game.turn_number <= 250 &&
            me->halite >= constants::SHIP_COST &&
            !game_map->at(me->shipyard)->is_occupied() &&
            is_shipyard_safe)
        {
            command_queue.push_back(me->shipyard->spawn());
        }

        if (!game.end_turn(command_queue)) break;
    }
    return 0;
}

/* 
Inventaire des fonctions pratiques pour donner des directives au bot :
    
    spawn() : fait apparaitre un petit vaisseau sur la case du vaisseau mère
    stay_still() : petit vaisseau reste sur place pour cette frame
    move() : déplace le vaisseau dans une direction donnée (cardinale)
    naive_navigate() : déplacement simple vers une cible

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