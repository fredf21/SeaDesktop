"""
locustfile_seadesktop.py — Test de charge SeaDesktop.

Scenario realistic mix (profil C) :
  - 70% : reads (GET list / GET by_id)
  - 20% : writes (POST create, PUT update)
  - 5%  : deletes (DELETE)
  - 5%  : auth (GET /auth/me)

Usage :
  # 1. Demarrer le backend avec load_test.yaml sur le port 8080
  # 2. Lancer Locust en mode UI :
  locust -f locustfile_seadesktop.py --host http://127.0.0.1:8080

  # Ou en mode headless (sans UI) :
  locust -f locustfile_seadesktop.py --host http://127.0.0.1:8080 \\
         --headless --users 50 --spawn-rate 5 --run-time 5m \\
         --csv=load_results --html=load_report.html

Metriques observees :
  - Latence p50/p95/p99 par endpoint
  - Throughput global (req/s)
  - Taux d'erreurs (4xx, 5xx, timeouts)
  - Si Locust UI : graphiques temps reel sur http://localhost:8089

A faire en parallele :
  - htop ou top -p <pid_backend> pour suivre RAM/CPU
  - Verifier que les shards Seastar travaillent (CPU bien reparti)
"""

import random
import uuid
from locust import HttpUser, task, between, events


# ─── Pool d'IDs de ressources créées (partagé entre users) ───
# On garde une liste limitée pour ne pas exploser la mémoire
# Locust quand le test tourne longtemps.
class ResourcePool:
    MAX_SIZE = 200  # cap pour éviter la croissance infinie
    
    def __init__(self):
        self.team_ids = []
        self.project_ids = []
        self.tag_ids = []
    
    def add_team(self, team_id):
        if len(self.team_ids) >= self.MAX_SIZE:
            # Remplace un id aléatoire (rotation)
            self.team_ids[random.randint(0, self.MAX_SIZE - 1)] = team_id
        else:
            self.team_ids.append(team_id)
    
    def add_project(self, project_id):
        if len(self.project_ids) >= self.MAX_SIZE:
            self.project_ids[random.randint(0, self.MAX_SIZE - 1)] = project_id
        else:
            self.project_ids.append(project_id)
    
    def add_tag(self, tag_id):
        if len(self.tag_ids) >= self.MAX_SIZE:
            self.tag_ids[random.randint(0, self.MAX_SIZE - 1)] = tag_id
        else:
            self.tag_ids.append(tag_id)
    
    def random_team(self):
        return random.choice(self.team_ids) if self.team_ids else None
    
    def random_project(self):
        return random.choice(self.project_ids) if self.project_ids else None
    
    def random_tag(self):
        return random.choice(self.tag_ids) if self.tag_ids else None


# Pool global partagé entre tous les users Locust
pool = ResourcePool()


class SeaDesktopUser(HttpUser):
    """Simule un user SeaDesktop authentifie avec un mix realistic."""
    
    # Pause entre 0.5 et 2.0 secondes entre les requetes
    # (simule un user humain, pas un bot)
    wait_time = between(0.5, 2.0)
    
    def on_start(self):
        """Au demarrage de chaque user simule : register + login.
        Le token est garde en memoire pour toutes les requetes."""
        self.email = f"loadtest-{uuid.uuid4().hex[:12]}@test.local"
        self.password = "LoadTest123!"
        self.token = None
        
        # Register
        with self.client.post(
            "/auth/register",
            json={"email": self.email, "password": self.password},
            name="/auth/register",
            catch_response=True
        ) as resp:
            if resp.status_code not in (200, 201):
                resp.failure(f"Register failed: {resp.status_code}")
                return
        
        # Login
        with self.client.post(
            "/auth/login",
            json={"email": self.email, "password": self.password},
            name="/auth/login",
            catch_response=True
        ) as resp:
            if resp.status_code != 200:
                resp.failure(f"Login failed: {resp.status_code}")
                return
            self.token = resp.json().get("access_token")
        
        # Pre-cree un Team et un Project pour avoir des ids stables
        # pour les reads futurs
        if self.token:
            self._create_initial_resources()
    
    def _create_initial_resources(self):
        """Pre-cree quelques ressources pour avoir des ids a utiliser."""
        headers = self._auth_headers()
        
        team_resp = self.client.post(
            "/teams",
            headers=headers,
            json={"name": f"InitTeam-{uuid.uuid4().hex[:6]}"},
            name="/teams (init)"
        )
        if team_resp.status_code in (200, 201):
            team_id = team_resp.json().get("id")
            if team_id:
                pool.add_team(team_id)
    
    def _auth_headers(self):
        return {"Authorization": f"Bearer {self.token}"} if self.token else {}
    
    # ─── 70% reads ────────────────────────────────────────────
    
    @task(35)
    def list_teams(self):
        """GET /teams — list, le plus frequent."""
        self.client.get("/teams", headers=self._auth_headers(),
                        name="/teams [list]")
    
    @task(20)
    def get_team_by_id(self):
        """GET /teams/{id} — read individuel."""
        team_id = pool.random_team()
        if team_id:
            self.client.get(f"/teams/{team_id}",
                            headers=self._auth_headers(),
                            name="/teams/[id]")
    
    @task(15)
    def list_projects(self):
        """GET /projects — list secondaire."""
        self.client.get("/projects", headers=self._auth_headers(),
                        name="/projects [list]")
    
    # ─── 20% writes ───────────────────────────────────────────
    
    @task(10)
    def create_team(self):
        """POST /teams — create."""
        with self.client.post(
            "/teams",
            headers=self._auth_headers(),
            json={"name": f"Team-{uuid.uuid4().hex[:8]}",
                  "description": "Load test team"},
            name="/teams [create]",
            catch_response=True
        ) as resp:
            if resp.status_code in (200, 201):
                team_id = resp.json().get("id")
                if team_id:
                    pool.add_team(team_id)
            elif resp.status_code not in (200, 201):
                resp.failure(f"Create failed: {resp.status_code}")
    
    @task(5)
    def create_project(self):
        """POST /projects — create d'un type secondaire."""
        with self.client.post(
            "/projects",
            headers=self._auth_headers(),
            json={"name": f"Project-{uuid.uuid4().hex[:8]}"},
            name="/projects [create]",
            catch_response=True
        ) as resp:
            if resp.status_code in (200, 201):
                project_id = resp.json().get("id")
                if project_id:
                    pool.add_project(project_id)
    
    @task(5)
    def update_team(self):
        """PUT /teams/{id} — update."""
        team_id = pool.random_team()
        if team_id:
            self.client.put(
                f"/teams/{team_id}",
                headers=self._auth_headers(),
                json={"name": f"UpdatedTeam-{uuid.uuid4().hex[:6]}",
                      "description": "Updated description"},
                name="/teams/[id] [update]"
            )
    
    # ─── 5% deletes ───────────────────────────────────────────
    
    @task(5)
    def delete_project(self):
        """DELETE /projects/{id} — delete (sur projects pour ne
        pas trop reduire le pool de teams)."""
        project_id = pool.random_project()
        if project_id:
            self.client.delete(
                f"/projects/{project_id}",
                headers=self._auth_headers(),
                name="/projects/[id] [delete]"
            )
    
    # ─── 5% auth ──────────────────────────────────────────────
    
    @task(5)
    def get_me(self):
        """GET /auth/me — verifie le token."""
        self.client.get("/auth/me", headers=self._auth_headers(),
                        name="/auth/me")


# Hook : print summary a la fin du test
@events.test_stop.add_listener
def on_test_stop(environment, **kwargs):
    stats = environment.stats
    print("\n" + "=" * 70)
    print("RÉSUMÉ FINAL")
    print("=" * 70)
    print(f"Total requests : {stats.total.num_requests}")
    print(f"Total failures : {stats.total.num_failures}")
    print(f"Median  : {stats.total.median_response_time}ms")
    print(f"p95     : {stats.total.get_response_time_percentile(0.95):.0f}ms")
    print(f"p99     : {stats.total.get_response_time_percentile(0.99):.0f}ms")
    print(f"RPS     : {stats.total.total_rps:.1f}")
    print(f"Pool sizes : teams={len(pool.team_ids)}, projects={len(pool.project_ids)}")
    print("=" * 70)
